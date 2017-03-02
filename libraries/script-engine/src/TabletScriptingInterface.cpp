//
//  Created by Anthony J. Thibault on 2016-12-12
//  Copyright 2013-2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "TabletScriptingInterface.h"

#include <QtCore/QThread>

#include <AccountManager.h>
#include "DependencyManager.h"
#include <PathUtils.h>
#include <QmlWindowClass.h>
#include <QQmlProperty>
#include <RegisteredMetaTypes.h>
#include "ScriptEngineLogging.h"
#include <OffscreenUi.h>
#include <InfoView.h>
#include "SoundEffect.h"

TabletScriptingInterface::TabletScriptingInterface() {
    qmlRegisterType<SoundEffect>("Hifi", 1, 0, "SoundEffect");
}

QObject* TabletScriptingInterface::getSystemToolbarProxy() {
    const QString SYSTEM_TOOLBAR = "com.highfidelity.interface.toolbar.system";
    Qt::ConnectionType connectionType = Qt::AutoConnection;
    if (QThread::currentThread() != _toolbarScriptingInterface->thread()) {
        connectionType = Qt::BlockingQueuedConnection;
    }
    QObject* toolbarProxy = nullptr;
    bool hasResult = QMetaObject::invokeMethod(_toolbarScriptingInterface, "getToolbar", connectionType, Q_RETURN_ARG(QObject*, toolbarProxy), Q_ARG(QString, SYSTEM_TOOLBAR));
    if (hasResult) {
        return toolbarProxy;
    } else {
        qCWarning(scriptengine) << "ToolbarScriptingInterface getToolbar has no result";
        return nullptr;
    }
}

QObject* TabletScriptingInterface::getTablet(const QString& tabletId) {

    std::lock_guard<std::mutex> guard(_mutex);

    // look up tabletId in the map.
    auto iter = _tabletProxies.find(tabletId);
    if (iter != _tabletProxies.end()) {
        // tablet already exists, just return it.
        return iter->second.data();
    } else {
        // allocate a new tablet, add it to the map then return it.
        auto tabletProxy = QSharedPointer<TabletProxy>(new TabletProxy(tabletId));
        _tabletProxies[tabletId] = tabletProxy;
        tabletProxy->setToolbarMode(_toolbarMode);
        return tabletProxy.data();
    }
}

void TabletScriptingInterface::setToolbarMode(bool toolbarMode) {
    std::lock_guard<std::mutex> guard(_mutex);

    _toolbarMode = toolbarMode;

    for (auto& iter : _tabletProxies) {
        iter.second->setToolbarMode(toolbarMode);
    }
}

void TabletScriptingInterface::setQmlTabletRoot(QString tabletId, QQuickItem* qmlTabletRoot, QObject* qmlOffscreenSurface) {
    TabletProxy* tablet = qobject_cast<TabletProxy*>(getTablet(tabletId));
    if (tablet) {
        tablet->setQmlTabletRoot(qmlTabletRoot, qmlOffscreenSurface);
    } else {
        qCWarning(scriptengine) << "TabletScriptingInterface::setupTablet() bad tablet object";
    }
}

QQuickWindow* TabletScriptingInterface::getTabletWindow() {
    TabletProxy* tablet = qobject_cast<TabletProxy*>(getTablet("com.highfidelity.interface.tablet.system"));
    QObject* qmlSurface = tablet->getTabletSurface();
    OffscreenQmlSurface* surface = dynamic_cast<OffscreenQmlSurface*>(qmlSurface);

    if (!surface) {
        return nullptr;
    }
    QQuickWindow* window = surface->getWindow();
    return window;
}

void TabletScriptingInterface::processMenuEvents(QObject* object, const QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Down:
            QMetaObject::invokeMethod(object, "nextItem");
            break;

        case Qt::Key_Up:
            QMetaObject::invokeMethod(object, "previousItem");
            break;

        case Qt::Key_Left:
            QMetaObject::invokeMethod(object, "previousPage");
            break;

        case Qt::Key_Right:
            QMetaObject::invokeMethod(object, "selectCurrentItem");
            break;

        case Qt::Key_Return:
            QMetaObject::invokeMethod(object, "selectCurrentItem");
            break;

        default:
            break;
    }
}

void TabletScriptingInterface::processTabletEvents(QObject* object, const QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Down:
            QMetaObject::invokeMethod(object, "downItem");
            break;

        case Qt::Key_Up:
            QMetaObject::invokeMethod(object, "upItem");
            break;

        case Qt::Key_Left:
            QMetaObject::invokeMethod(object, "previousItem");
            break;

        case Qt::Key_Right:
            QMetaObject::invokeMethod(object, "nextItem");
            break;

        case Qt::Key_Return:
            QMetaObject::invokeMethod(object, "selectItem");
            break;

        default:
            break;
    }
}


void TabletScriptingInterface::processEvent(const QKeyEvent* event) {
    TabletProxy* tablet = qobject_cast<TabletProxy*>(getTablet("com.highfidelity.interface.tablet.system"));
    QObject* qmlTablet = tablet->getQmlTablet();
    QObject* qmlMenu = tablet->getQmlMenu();

    if (qmlTablet) {
        processTabletEvents(qmlTablet, event);
    } else if (qmlMenu) {
        processMenuEvents(qmlMenu, event);
    }
}

QObject* TabletScriptingInterface::getFlags()
{
    auto offscreenUi = DependencyManager::get<OffscreenUi>();
    return offscreenUi->getFlags();
}

//
// TabletProxy
//

static const char* TABLET_SOURCE_URL = "Tablet.qml";
static const char* WEB_VIEW_SOURCE_URL = "TabletWebView.qml";
static const char* VRMENU_SOURCE_URL = "TabletMenu.qml";

class TabletRootWindow : public QmlWindowClass {
    virtual QString qmlSource() const override { return "hifi/tablet/WindowRoot.qml"; }
};

TabletProxy::TabletProxy(QString name) : _name(name) {

}

void TabletProxy::setToolbarMode(bool toolbarMode) {
    if (toolbarMode == _toolbarMode) {
        return;
    }

    _toolbarMode = toolbarMode;

    if (toolbarMode) {
        removeButtonsFromHomeScreen();
        addButtonsToToolbar();

        // create new desktop window
        auto offscreenUi = DependencyManager::get<OffscreenUi>();
        offscreenUi->executeOnUiThread([=] {
            auto tabletRootWindow = new TabletRootWindow();
            tabletRootWindow->initQml(QVariantMap());
            auto quickItem = tabletRootWindow->asQuickItem();
            _desktopWindow = tabletRootWindow;
            QMetaObject::invokeMethod(quickItem, "setShown", Q_ARG(const QVariant&, QVariant(false)));

            QObject::connect(quickItem, SIGNAL(windowClosed()), this, SLOT(desktopWindowClosed()));

            QObject::connect(tabletRootWindow, SIGNAL(webEventReceived(QVariant)), this, SIGNAL(webEventReceived(QVariant)));

            // forward qml surface events to interface js
            connect(tabletRootWindow, &QmlWindowClass::fromQml, this, &TabletProxy::fromQml);
        });
    } else {
        removeButtonsFromToolbar();
        addButtonsToHomeScreen();

        // destroy desktop window
        if (_desktopWindow) {
            _desktopWindow->deleteLater();
            _desktopWindow = nullptr;
        }
    }
}

static void addButtonProxyToQmlTablet(QQuickItem* qmlTablet, TabletButtonProxy* buttonProxy) {
    QVariant resultVar;
    Qt::ConnectionType connectionType = Qt::AutoConnection;
    if (QThread::currentThread() != qmlTablet->thread()) {
        connectionType = Qt::BlockingQueuedConnection;
    }
    bool hasResult = QMetaObject::invokeMethod(qmlTablet, "addButtonProxy", connectionType,
                                               Q_RETURN_ARG(QVariant, resultVar), Q_ARG(QVariant, buttonProxy->getProperties()));
    if (!hasResult) {
        qCWarning(scriptengine) << "TabletScriptingInterface addButtonProxyToQmlTablet has no result";
        return;
    }

    QObject* qmlButton = qvariant_cast<QObject *>(resultVar);
    if (!qmlButton) {
        qCWarning(scriptengine) << "TabletScriptingInterface addButtonProxyToQmlTablet result not a QObject";
        return;
    }
    QObject::connect(qmlButton, SIGNAL(clicked()), buttonProxy, SLOT(clickedSlot()));
    buttonProxy->setQmlButton(qobject_cast<QQuickItem*>(qmlButton));
}

static QString getUsername() {
    QString username = "Unknown user";
    auto accountManager = DependencyManager::get<AccountManager>();
    if (accountManager->isLoggedIn()) {
        return accountManager->getAccountInfo().getUsername();
    } else {
        return "Unknown user";
    }
}

void TabletProxy::setQmlTabletRoot(QQuickItem* qmlTabletRoot, QObject* qmlOffscreenSurface) {
    std::lock_guard<std::mutex> guard(_mutex);
    _qmlOffscreenSurface = qmlOffscreenSurface;
    _qmlTabletRoot = qmlTabletRoot;
    if (_qmlTabletRoot && _qmlOffscreenSurface) {
        QObject::connect(_qmlOffscreenSurface, SIGNAL(webEventReceived(QVariant)), this, SIGNAL(webEventReceived(QVariant)));

        // forward qml surface events to interface js
        connect(dynamic_cast<OffscreenQmlSurface*>(_qmlOffscreenSurface), &OffscreenQmlSurface::fromQml, [this](QVariant message) {
            if (message.canConvert<QJSValue>()) {
                emit fromQml(qvariant_cast<QJSValue>(message).toVariant());
            } else if (message.canConvert<QString>()) {
                emit fromQml(message.toString());
            } else {
                qWarning() << "fromQml: Unsupported message type " << message;
            }
        });

        if (_toolbarMode) {
            // if someone creates the tablet in toolbar mode, make sure to display the home screen on the tablet.
            auto loader = _qmlTabletRoot->findChild<QQuickItem*>("loader");
            QObject::connect(loader, SIGNAL(loaded()), this, SLOT(addButtonsToHomeScreen()), Qt::DirectConnection);
            QMetaObject::invokeMethod(_qmlTabletRoot, "loadSource", Q_ARG(const QVariant&, QVariant(TABLET_SOURCE_URL)));
        }

        gotoHomeScreen();

        QMetaObject::invokeMethod(_qmlTabletRoot, "setUsername", Q_ARG(const QVariant&, QVariant(getUsername())));

        // hook up username changed signal.
        auto accountManager = DependencyManager::get<AccountManager>();
        QObject::connect(accountManager.data(), &AccountManager::profileChanged, [this]() {
            if (_qmlTabletRoot) {
                QMetaObject::invokeMethod(_qmlTabletRoot, "setUsername", Q_ARG(const QVariant&, QVariant(getUsername())));
            }
        });
    } else {
        removeButtonsFromHomeScreen();
        _state = State::Uninitialized;
        emit screenChanged(QVariant("Closed"), QVariant(""));
    }
}

void TabletProxy::gotoMenuScreen(const QString& submenu) {

    QObject* root = nullptr;
    if (!_toolbarMode && _qmlTabletRoot) {
        root = _qmlTabletRoot;
    } else if (_toolbarMode && _desktopWindow) {
        root = _desktopWindow->asQuickItem();
    }

    if (root) {
        removeButtonsFromHomeScreen();
        auto offscreenUi = DependencyManager::get<OffscreenUi>();
        QObject* menu = offscreenUi->getRootMenu();
        QMetaObject::invokeMethod(root, "setMenuProperties", Q_ARG(QVariant, QVariant::fromValue(menu)), Q_ARG(const QVariant&, QVariant(submenu)));
        QMetaObject::invokeMethod(root, "loadSource", Q_ARG(const QVariant&, QVariant(VRMENU_SOURCE_URL)));
        _state = State::Menu;
        emit screenChanged(QVariant("Menu"), QVariant(VRMENU_SOURCE_URL));
        QMetaObject::invokeMethod(root, "setShown", Q_ARG(const QVariant&, QVariant(true)));
    }
}

void TabletProxy::loadQMLSource(const QVariant& path) {

    QObject* root = nullptr;
    if (!_toolbarMode && _qmlTabletRoot) {
        root = _qmlTabletRoot;
    } else if (_toolbarMode && _desktopWindow) {
        root = _desktopWindow->asQuickItem();
    }

    if (root) {
        if (_state != State::QML) {
            removeButtonsFromHomeScreen();
            QMetaObject::invokeMethod(root, "loadSource", Q_ARG(const QVariant&, path));
            _state = State::QML;
            emit screenChanged(QVariant("QML"), path);
            QMetaObject::invokeMethod(root, "setShown", Q_ARG(const QVariant&, QVariant(true)));
        }
    }
}

void TabletProxy::gotoHomeScreen() {
    if (_state != State::Home) {
        if (!_toolbarMode && _qmlTabletRoot) {
            auto loader = _qmlTabletRoot->findChild<QQuickItem*>("loader");
            QObject::connect(loader, SIGNAL(loaded()), this, SLOT(addButtonsToHomeScreen()), Qt::DirectConnection);
            QMetaObject::invokeMethod(_qmlTabletRoot, "loadSource", Q_ARG(const QVariant&, QVariant(TABLET_SOURCE_URL)));
            QMetaObject::invokeMethod(_qmlTabletRoot, "playButtonClickSound");
        } else if (_toolbarMode && _desktopWindow) {
            // close desktop window
            if (_desktopWindow->asQuickItem()) {
                QMetaObject::invokeMethod(_desktopWindow->asQuickItem(), "setShown", Q_ARG(const QVariant&, QVariant(false)));
            }
        }
        _state = State::Home;
        emit screenChanged(QVariant("Home"), QVariant(TABLET_SOURCE_URL));
    }
}

void TabletProxy::gotoWebScreen(const QString& url) {
    gotoWebScreen(url, "");
}

void TabletProxy::gotoWebScreen(const QString& url, const QString& injectedJavaScriptUrl) {

    QObject* root = nullptr;
    if (!_toolbarMode && _qmlTabletRoot) {
        root = _qmlTabletRoot;
    } else if (_toolbarMode && _desktopWindow) {
        root = _desktopWindow->asQuickItem();
    }

    if (root) {
        removeButtonsFromHomeScreen();
        QMetaObject::invokeMethod(root, "loadSource", Q_ARG(const QVariant&, QVariant(WEB_VIEW_SOURCE_URL)));
        QMetaObject::invokeMethod(root, "setShown", Q_ARG(const QVariant&, QVariant(true)));
        QMetaObject::invokeMethod(root, "loadWebUrl", Q_ARG(const QVariant&, QVariant(url)), Q_ARG(const QVariant&, QVariant(injectedJavaScriptUrl)));
    }
    _state = State::Web;
    emit screenChanged(QVariant("Web"), QVariant(url));
}

QObject* TabletProxy::addButton(const QVariant& properties) {
    auto tabletButtonProxy = QSharedPointer<TabletButtonProxy>(new TabletButtonProxy(properties.toMap()));
    std::lock_guard<std::mutex> guard(_mutex);
    _tabletButtonProxies.push_back(tabletButtonProxy);
    if (!_toolbarMode && _qmlTabletRoot) {
        auto tablet = getQmlTablet();
        if (tablet) {
            addButtonProxyToQmlTablet(tablet, tabletButtonProxy.data());
        } else {
            qCCritical(scriptengine) << "Could not find tablet in TabletRoot.qml";
        }
    } else if (_toolbarMode) {

        auto tabletScriptingInterface = DependencyManager::get<TabletScriptingInterface>();
        QObject* toolbarProxy = tabletScriptingInterface->getSystemToolbarProxy();

        Qt::ConnectionType connectionType = Qt::AutoConnection;
        if (QThread::currentThread() != toolbarProxy->thread()) {
            connectionType = Qt::BlockingQueuedConnection;
        }

        // copy properties from tablet button proxy to toolbar button proxy.
        QObject* toolbarButtonProxy = nullptr;
        bool hasResult = QMetaObject::invokeMethod(toolbarProxy, "addButton", connectionType, Q_RETURN_ARG(QObject*, toolbarButtonProxy), Q_ARG(QVariant, tabletButtonProxy->getProperties()));
        if (hasResult) {
            tabletButtonProxy->setToolbarButtonProxy(toolbarButtonProxy);
        } else {
            qCWarning(scriptengine) << "ToolbarProxy addButton has no result";
        }
    }
    return tabletButtonProxy.data();
}

bool TabletProxy::onHomeScreen() {
    return _state == State::Home;
}

void TabletProxy::removeButton(QObject* tabletButtonProxy) {
    std::lock_guard<std::mutex> guard(_mutex);

    auto tablet = getQmlTablet();
    if (!tablet) {
        qCCritical(scriptengine) << "Could not find tablet in TabletRoot.qml";
    }

    auto iter = std::find(_tabletButtonProxies.begin(), _tabletButtonProxies.end(), tabletButtonProxy);
    if (iter != _tabletButtonProxies.end()) {
        if (!_toolbarMode && _qmlTabletRoot) {
            (*iter)->setQmlButton(nullptr);
            if (tablet) {
                QMetaObject::invokeMethod(tablet, "removeButtonProxy", Qt::AutoConnection, Q_ARG(QVariant, (*iter)->getProperties()));
            }
        } else if (_toolbarMode) {
            auto tabletScriptingInterface = DependencyManager::get<TabletScriptingInterface>();
            QObject* toolbarProxy = tabletScriptingInterface->getSystemToolbarProxy();

            // remove button from toolbarProxy
            QMetaObject::invokeMethod(toolbarProxy, "removeButton", Qt::AutoConnection, Q_ARG(QVariant, (*iter)->getUuid().toString()));
            (*iter)->setToolbarButtonProxy(nullptr);
        }
        _tabletButtonProxies.erase(iter);
    } else {
        qCWarning(scriptengine) << "TabletProxy::removeButton() could not find button " << tabletButtonProxy;
    }
}

void TabletProxy::updateMicEnabled(const bool micOn) {
    auto tablet = getQmlTablet();
    if (!tablet) {
        //qCCritical(scriptengine) << "Could not find tablet in TabletRoot.qml";
    } else {
        QMetaObject::invokeMethod(tablet, "setMicEnabled", Qt::AutoConnection, Q_ARG(QVariant, QVariant(micOn)));
    }
}

void TabletProxy::updateAudioBar(const double micLevel) {
    auto tablet = getQmlTablet();
    if (!tablet) {
        //qCCritical(scriptengine) << "Could not find tablet in TabletRoot.qml";
    } else {
        QMetaObject::invokeMethod(tablet, "setMicLevel", Qt::AutoConnection, Q_ARG(QVariant, QVariant(micLevel)));
    }
}

void TabletProxy::emitScriptEvent(QVariant msg) {
    if (!_toolbarMode && _qmlOffscreenSurface) {
        QMetaObject::invokeMethod(_qmlOffscreenSurface, "emitScriptEvent", Qt::AutoConnection, Q_ARG(QVariant, msg));
    } else if (_toolbarMode && _desktopWindow) {
        QMetaObject::invokeMethod(_desktopWindow, "emitScriptEvent", Qt::AutoConnection, Q_ARG(QVariant, msg));
    }
}

void TabletProxy::sendToQml(QVariant msg) {
    if (!_toolbarMode && _qmlOffscreenSurface) {
        QMetaObject::invokeMethod(_qmlOffscreenSurface, "sendToQml", Qt::AutoConnection, Q_ARG(QVariant, msg));
    } else if (_toolbarMode && _desktopWindow) {
        QMetaObject::invokeMethod(_desktopWindow, "sendToQml", Qt::AutoConnection, Q_ARG(QVariant, msg));
    }
}

void TabletProxy::addButtonsToHomeScreen() {
    auto tablet = getQmlTablet();
    if (!tablet || _toolbarMode) {
        return;
    }

    auto tabletScriptingInterface = DependencyManager::get<TabletScriptingInterface>();
    for (auto& buttonProxy : _tabletButtonProxies) {
        addButtonProxyToQmlTablet(tablet, buttonProxy.data());
    }
     auto loader = _qmlTabletRoot->findChild<QQuickItem*>("loader");
     QObject::disconnect(loader, SIGNAL(loaded()), this, SLOT(addButtonsToHomeScreen()));
}

QObject* TabletProxy::getTabletSurface() {
    return _qmlOffscreenSurface;
}

void TabletProxy::removeButtonsFromHomeScreen() {
    auto tablet = getQmlTablet();
    for (auto& buttonProxy : _tabletButtonProxies) {
        if (tablet) {
            QMetaObject::invokeMethod(tablet, "removeButtonProxy", Qt::AutoConnection, Q_ARG(QVariant, buttonProxy->getProperties()));
        }
        buttonProxy->setQmlButton(nullptr);
    }
}

void TabletProxy::desktopWindowClosed() {
    gotoHomeScreen();
}

void TabletProxy::addButtonsToToolbar() {
    auto tabletScriptingInterface = DependencyManager::get<TabletScriptingInterface>();
    QObject* toolbarProxy = tabletScriptingInterface->getSystemToolbarProxy();

    Qt::ConnectionType connectionType = Qt::AutoConnection;
    if (QThread::currentThread() != toolbarProxy->thread()) {
        connectionType = Qt::BlockingQueuedConnection;
    }

    for (auto& buttonProxy : _tabletButtonProxies) {
        // copy properties from tablet button proxy to toolbar button proxy.
        QObject* toolbarButtonProxy = nullptr;
        bool hasResult = QMetaObject::invokeMethod(toolbarProxy, "addButton", connectionType, Q_RETURN_ARG(QObject*, toolbarButtonProxy), Q_ARG(QVariant, buttonProxy->getProperties()));
        if (hasResult) {
            buttonProxy->setToolbarButtonProxy(toolbarButtonProxy);
        } else {
            qCWarning(scriptengine) << "ToolbarProxy addButton has no result";
        }
    }

    // make the toolbar visible
    QMetaObject::invokeMethod(toolbarProxy, "writeProperty", Qt::AutoConnection, Q_ARG(QString, "visible"), Q_ARG(QVariant, QVariant(true)));
}

void TabletProxy::removeButtonsFromToolbar() {
    auto tabletScriptingInterface = DependencyManager::get<TabletScriptingInterface>();
    QObject* toolbarProxy = tabletScriptingInterface->getSystemToolbarProxy();
    for (auto& buttonProxy : _tabletButtonProxies) {
        // remove button from toolbarProxy
        QMetaObject::invokeMethod(toolbarProxy, "removeButton", Qt::AutoConnection, Q_ARG(QVariant, buttonProxy->getUuid().toString()));
        buttonProxy->setToolbarButtonProxy(nullptr);
    }
}

QQuickItem* TabletProxy::getQmlTablet() const {
    if (!_qmlTabletRoot) {
        return nullptr;
    }

    auto loader = _qmlTabletRoot->findChild<QQuickItem*>("loader");
    if (!loader) {
        return nullptr;
    }

    auto tablet = loader->findChild<QQuickItem*>("tablet");
    if (!tablet) {
        return nullptr;
    }

    return tablet;
}

QQuickItem* TabletProxy::getQmlMenu() const {
     if (!_qmlTabletRoot) {
        return nullptr;
    }

    auto loader = _qmlTabletRoot->findChild<QQuickItem*>("loader");
    if (!loader) {
        return nullptr;
    }

    QQuickItem* VrMenu = loader->findChild<QQuickItem*>("tabletMenu");
    if (!VrMenu) {
        return nullptr;
    }

    QQuickItem* menuList = VrMenu->findChild<QQuickItem*>("tabletMenuHandlerItem");
    if (!menuList) {
        return nullptr;
    }
    return menuList;
}

//
// TabletButtonProxy
//

const QString UUID_KEY = "uuid";
const QString OBJECT_NAME_KEY = "objectName";
const QString STABLE_ORDER_KEY = "stableOrder";
static int s_stableOrder = 1;

TabletButtonProxy::TabletButtonProxy(const QVariantMap& properties) : _uuid(QUuid::createUuid()), _stableOrder(++s_stableOrder), _properties(properties) {
    // this is used to uniquely identify this button.
    _properties[UUID_KEY] = _uuid;
    _properties[OBJECT_NAME_KEY] = _uuid.toString();
    _properties[STABLE_ORDER_KEY] = _stableOrder;
}

void TabletButtonProxy::setQmlButton(QQuickItem* qmlButton) {
    std::lock_guard<std::mutex> guard(_mutex);
    _qmlButton = qmlButton;
}

void TabletButtonProxy::setToolbarButtonProxy(QObject* toolbarButtonProxy) {
    std::lock_guard<std::mutex> guard(_mutex);
    _toolbarButtonProxy = toolbarButtonProxy;
    if (_toolbarButtonProxy) {
        QObject::connect(_toolbarButtonProxy, SIGNAL(clicked()), this, SLOT(clickedSlot()));
    }
}

QVariantMap TabletButtonProxy::getProperties() const {
    std::lock_guard<std::mutex> guard(_mutex);
    return _properties;
}

void TabletButtonProxy::editProperties(QVariantMap properties) {
    std::lock_guard<std::mutex> guard(_mutex);

    QVariantMap::const_iterator iter = properties.constBegin();
    while (iter != properties.constEnd()) {
        _properties[iter.key()] = iter.value();
        if (_qmlButton) {
            QMetaObject::invokeMethod(_qmlButton, "changeProperty", Qt::AutoConnection, Q_ARG(QVariant, QVariant(iter.key())), Q_ARG(QVariant, iter.value()));
        }
        ++iter;
    }

    if (_toolbarButtonProxy) {
        QMetaObject::invokeMethod(_toolbarButtonProxy, "editProperties", Qt::AutoConnection, Q_ARG(QVariantMap, properties));
    }
}

#include "TabletScriptingInterface.moc"

