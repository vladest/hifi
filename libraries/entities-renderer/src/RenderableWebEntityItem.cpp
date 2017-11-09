//
//  Created by Bradley Austin Davis on 2015/05/12
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "RenderableWebEntityItem.h"

#include <QtCore/QTimer>
#include <QtGui/QOpenGLContext>
#include <QtGui/QTouchDevice>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQml/QQmlContext>


#include <GeometryCache.h>
#include <PathUtils.h>
#include <PointerEvent.h>
#include <gl/GLHelpers.h>
#include <ui/OffscreenQmlSurface.h>
#include <ui/TabletScriptingInterface.h>
#include <EntityScriptingInterface.h>

#include "EntitiesRendererLogging.h"


using namespace render;
using namespace render::entities;

const float METERS_TO_INCHES = 39.3701f;
static uint32_t _currentWebCount{ 0 };
// Don't allow more than 100 concurrent web views
static const uint32_t MAX_CONCURRENT_WEB_VIEWS = 20;
// If a web-view hasn't been rendered for 30 seconds, de-allocate the framebuffer
static uint64_t MAX_NO_RENDER_INTERVAL = 30 * USECS_PER_SECOND;

static int MAX_WINDOW_SIZE = 4096;
static float OPAQUE_ALPHA_THRESHOLD = 0.99f;
static int DEFAULT_MAX_FPS = 10;
static int YOUTUBE_MAX_FPS = 30;

static QTouchDevice _touchDevice;

WebEntityRenderer::WebEntityRenderer(const EntityItemPointer& entity) : Parent(entity) {
    static std::once_flag once;
    std::call_once(once, [&]{
        _touchDevice.setCapabilities(QTouchDevice::Position);
        _touchDevice.setType(QTouchDevice::TouchScreen);
        _touchDevice.setName("WebEntityRendererTouchDevice");
        _touchDevice.setMaximumTouchPoints(4);
    });
    _geometryId = DependencyManager::get<GeometryCache>()->allocateID();
    _texture = gpu::Texture::createExternal(OffscreenQmlSurface::getDiscardLambda());
    _texture->setSource(__FUNCTION__);
    _timer.setInterval(MSECS_PER_SECOND);
    connect(&_timer, &QTimer::timeout, this, &WebEntityRenderer::onTimeout);
}

void WebEntityRenderer::onRemoveFromSceneTyped(const TypedEntityPointer& entity) {
    destroyWebSurface();

    auto geometryCache = DependencyManager::get<GeometryCache>();
    if (geometryCache) {
        geometryCache->releaseID(_geometryId);
    }
}

bool WebEntityRenderer::needsRenderUpdateFromTypedEntity(const TypedEntityPointer& entity) const {
    if (_contextPosition != entity->getPosition()) {
        return true;
    }

    if (uvec2(getWindowSize(entity)) != toGlm(_webSurface->size())) {
        return true;
    }

    if (_lastSourceUrl != entity->getSourceUrl()) {
        return true;
    }

    if (_lastDPI != entity->getDPI()) {
        return true;
    }

    if (_lastLocked != entity->getLocked()) {
        return true;
    }

    return false;
}

bool WebEntityRenderer::needsRenderUpdate() const {
    if (!_webSurface) {
        // If we have rendered recently, and there is no web surface, we're going to create one
        return true;
    }

    return Parent::needsRenderUpdate();
}

void WebEntityRenderer::onTimeout() {
    bool needsCheck = resultWithReadLock<bool>([&] {
        return (_lastRenderTime != 0 && (bool)_webSurface);
    });

    if (!needsCheck) {
        return;
    }

    uint64_t interval;
    withReadLock([&] {
        interval = usecTimestampNow() - _lastRenderTime;
    });

    if (interval > MAX_NO_RENDER_INTERVAL) {
        destroyWebSurface();
    }
}

void WebEntityRenderer::doRenderUpdateSynchronousTyped(const ScenePointer& scene, Transaction& transaction, const TypedEntityPointer& entity) {
    withWriteLock([&] {
        // This work must be done on the main thread
        if (!hasWebSurface()) {
            buildWebSurface(entity);
        }

        if (_contextPosition != entity->getPosition()) {
            // update globalPosition
            _contextPosition = entity->getPosition();
            _webSurface->getSurfaceContext()->setContextProperty("globalPosition", vec3toVariant(_contextPosition));
        }

        if (_lastSourceUrl != entity->getSourceUrl()) {
            _lastSourceUrl = entity->getSourceUrl();
            loadSourceURL();
        }

        _lastDPI = entity->getDPI();
        _lastLocked = entity->getLocked();

        glm::vec2 windowSize = getWindowSize(entity);
        _webSurface->resize(QSize(windowSize.x, windowSize.y));
        _renderTransform = getModelTransform();
        _renderTransform.postScale(entity->getDimensions());
    });
}

void WebEntityRenderer::doRender(RenderArgs* args) {
    withWriteLock([&] {
        _lastRenderTime = usecTimestampNow();
    });

#ifdef WANT_EXTRA_DEBUGGING
    {
        gpu::Batch& batch = *args->_batch;
        batch.setModelTransform(getTransformToCenter()); // we want to include the scale as well
        glm::vec4 cubeColor{ 1.0f, 0.0f, 0.0f, 1.0f };
        DependencyManager::get<GeometryCache>()->renderWireCube(batch, 1.0f, cubeColor);
    }
#endif

    // Try to update the texture
    {
        QSharedPointer<OffscreenQmlSurface> webSurface;
        withReadLock([&] {
            webSurface = _webSurface;
        });
        if (!webSurface) {
            return;
        }

        OffscreenQmlSurface::TextureAndFence newTextureAndFence;
        bool newTextureAvailable = webSurface->fetchTexture(newTextureAndFence);
        if (newTextureAvailable) {
            _texture->setExternalTexture(newTextureAndFence.first, newTextureAndFence.second);
        }
    }

    PerformanceTimer perfTimer("WebEntityRenderer::render");
    static const glm::vec2 texMin(0.0f), texMax(1.0f), topLeft(-0.5f), bottomRight(0.5f);

    gpu::Batch& batch = *args->_batch;
    withReadLock([&] {
        batch.setModelTransform(_renderTransform);
    });
    batch.setResourceTexture(0, _texture);
    float fadeRatio = _isFading ? Interpolate::calculateFadeRatio(_fadeStartTime) : 1.0f;
    batch._glColor4f(1.0f, 1.0f, 1.0f, fadeRatio);

    DependencyManager::get<GeometryCache>()->bindWebBrowserProgram(batch, fadeRatio < OPAQUE_ALPHA_THRESHOLD);
    DependencyManager::get<GeometryCache>()->renderQuad(batch, topLeft, bottomRight, texMin, texMax, glm::vec4(1.0f, 1.0f, 1.0f, fadeRatio), _geometryId);
}

bool WebEntityRenderer::hasWebSurface() {
    return (bool)_webSurface;
}

bool WebEntityRenderer::buildWebSurface(const TypedEntityPointer& entity) {
    if (_currentWebCount >= MAX_CONCURRENT_WEB_VIEWS) {
        qWarning() << "Too many concurrent web views to create new view";
        return false;
    }

    ++_currentWebCount;
    auto deleter = [](OffscreenQmlSurface* webSurface) {
        AbstractViewStateInterface::instance()->postLambdaEvent([webSurface] {
            if (AbstractViewStateInterface::instance()->isAboutToQuit()) {
                // WebEngineView may run other threads (wasapi), so they must be deleted for a clean shutdown
                // if the application has already stopped its event loop, delete must be explicit
                delete webSurface;
            } else {
                webSurface->deleteLater();
            }
        });
    };

    {
        _webSurface = QSharedPointer<OffscreenQmlSurface>(new OffscreenQmlSurface(), deleter);
        _webSurface->create();
    }

    // FIXME, the max FPS could be better managed by being dynamic (based on the number of current surfaces
    // and the current rendering load)
    _webSurface->setMaxFps(DEFAULT_MAX_FPS);
    // FIXME - Keyboard HMD only: Possibly add "HMDinfo" object to context for WebView.qml.
    _webSurface->getSurfaceContext()->setContextProperty("desktop", QVariant());
    _fadeStartTime = usecTimestampNow();
    loadSourceURL();
    _webSurface->resume();

    // forward web events to EntityScriptingInterface
    auto entities = DependencyManager::get<EntityScriptingInterface>();
    const EntityItemID entityItemID = entity->getID();
    QObject::connect(_webSurface.data(), &OffscreenQmlSurface::webEventReceived, [=](const QVariant& message) {
        emit entities->webEventReceived(entityItemID, message);
    });

    return true;
}

void WebEntityRenderer::destroyWebSurface() {
    QSharedPointer<OffscreenQmlSurface> webSurface;
    withWriteLock([&] {
        webSurface.swap(_webSurface);
    });

    if (webSurface) {
        --_currentWebCount;
        QQuickItem* rootItem = webSurface->getRootItem();

        if (rootItem && rootItem->objectName() == "tabletRoot") {
            auto tabletScriptingInterface = DependencyManager::get<TabletScriptingInterface>();
            tabletScriptingInterface->setQmlTabletRoot("com.highfidelity.interface.tablet.system", nullptr);
        }

        // Fix for crash in QtWebEngineCore when rapidly switching domains
        // Call stop on the QWebEngineView before destroying OffscreenQMLSurface.
        if (rootItem) {
            QObject* obj = rootItem->findChild<QObject*>("webEngineView");
            if (obj) {
                // stop loading
                QMetaObject::invokeMethod(obj, "stop");
            }
        }

        webSurface->pause();
        webSurface.reset();
    }
}

glm::vec2 WebEntityRenderer::getWindowSize(const TypedEntityPointer& entity) const {
    glm::vec2 dims = glm::vec2(entity->getDimensions());
    dims *= METERS_TO_INCHES * _lastDPI;

    // ensure no side is never larger then MAX_WINDOW_SIZE
    float max = (dims.x > dims.y) ? dims.x : dims.y;
    if (max > MAX_WINDOW_SIZE) {
        dims *= MAX_WINDOW_SIZE / max;
    }

    return dims;
}

void WebEntityRenderer::loadSourceURL() {
    const QUrl sourceUrl(_lastSourceUrl);
    if (sourceUrl.scheme() == "http" || sourceUrl.scheme() == "https" ||
        _lastSourceUrl.toLower().endsWith(".htm") || _lastSourceUrl.toLower().endsWith(".html")) {
        _contentType = htmlContent;
        _webSurface->setBaseUrl(QUrl::fromLocalFile(PathUtils::resourcesPath() + "qml/controls/"));

        // We special case YouTube URLs since we know they are videos that we should play with at least 30 FPS.
        if (sourceUrl.host().endsWith("youtube.com", Qt::CaseInsensitive)) {
            _webSurface->setMaxFps(YOUTUBE_MAX_FPS);
        } else {
            _webSurface->setMaxFps(DEFAULT_MAX_FPS);
        }

        _webSurface->load("WebEntityView.qml", [this](QQmlContext* context, QObject* item) {
            item->setProperty("url", _lastSourceUrl);
        });
    } else {
        _contentType = qmlContent;
        _webSurface->setBaseUrl(QUrl::fromLocalFile(PathUtils::resourcesPath()));
        _webSurface->load(_lastSourceUrl);
        if (_webSurface->getRootItem() && _webSurface->getRootItem()->objectName() == "tabletRoot") {
            auto tabletScriptingInterface = DependencyManager::get<TabletScriptingInterface>();
            tabletScriptingInterface->setQmlTabletRoot("com.highfidelity.interface.tablet.system", _webSurface.data());
        }
    }
}

void WebEntityRenderer::hoverLeaveEntity(const PointerEvent& event) {
    if (!_lastLocked && _webSurface && _touchBeginAccepted) {
        // If the user mouses off the entity while the button is down, simulate a touch end.
        QTouchEvent::TouchPoint point;
        point.setId(event.getID());
        point.setState(Qt::TouchPointReleased);
        glm::vec2 windowPos = event.getPos2D() * (METERS_TO_INCHES * _lastDPI);
        QPointF windowPoint(windowPos.x, windowPos.y);
        point.setScenePos(windowPoint);
        point.setPos(windowPoint);
        QList<QTouchEvent::TouchPoint> touchPoints;
        touchPoints.push_back(point);
        QTouchEvent* touchEvent = new QTouchEvent(QEvent::TouchEnd, nullptr,
            Qt::NoModifier, Qt::TouchPointReleased, touchPoints);
        touchEvent->setWindow(_webSurface->getWindow());
        touchEvent->setDevice(&_touchDevice);
        touchEvent->setTarget(_webSurface->getRootItem());
        QCoreApplication::postEvent(_webSurface->getWindow(), touchEvent);
        _activeTouchPoints.clear();
    }
}

void WebEntityRenderer::handlePointerEvent(const PointerEvent& event) {
    // Ignore mouse interaction if we're locked
    if (_lastLocked || !_webSurface) {
        return;
    }

    QPointF windowPoint;
    {
        glm::vec2 windowPos = event.getPos2D() * (METERS_TO_INCHES * _lastDPI);
        windowPoint = QPointF(windowPos.x, windowPos.y);
    }

    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons = Qt::NoButton;
    if (event.getButton() == PointerEvent::PrimaryButton) {
        button = Qt::LeftButton;
    }
    if (event.getButtons() & PointerEvent::PrimaryButton) {
        buttons |= Qt::LeftButton;
    }

    if (event.getType() == PointerEvent::Move) {
        // Forward a mouse move event to webSurface
        QMouseEvent* mouseEvent = new QMouseEvent(QEvent::MouseMove, windowPoint, windowPoint, windowPoint, button, buttons, Qt::NoModifier);
        QCoreApplication::postEvent(_webSurface->getWindow(), mouseEvent);
    }

    Qt::TouchPointState state = Qt::TouchPointStationary;
    if (event.getType() == PointerEvent::Press && event.getButton() == PointerEvent::PrimaryButton) {
        state = Qt::TouchPointPressed;
    } else if (event.getType() == PointerEvent::Release) {
        state = Qt::TouchPointReleased;
    } else if (_activeTouchPoints.count(event.getID()) && windowPoint != _activeTouchPoints[event.getID()].pos()) {
        state = Qt::TouchPointMoved;
    }

    QEvent::Type touchType = QEvent::TouchUpdate;
    if (_activeTouchPoints.empty()) {
        // If the first active touch point is being created, send a begin
        touchType = QEvent::TouchBegin;
    } if (state == Qt::TouchPointReleased && _activeTouchPoints.size() == 1 && _activeTouchPoints.count(event.getID())) {
        // If the last active touch point is being released, send an end
        touchType = QEvent::TouchEnd;
    }
    {
        QTouchEvent::TouchPoint point;
        point.setId(event.getID());
        point.setState(state);
        point.setPos(windowPoint);
        point.setScreenPos(windowPoint);
        _activeTouchPoints[event.getID()] = point;
    }

    QTouchEvent touchEvent(touchType, &_touchDevice, event.getKeyboardModifiers());
    {
        QList<QTouchEvent::TouchPoint> touchPoints;
        Qt::TouchPointStates touchPointStates;
        for (const auto& entry : _activeTouchPoints) {
            touchPointStates |= entry.second.state();
            touchPoints.push_back(entry.second);
        }

        touchEvent.setWindow(_webSurface->getWindow());
        touchEvent.setTarget(_webSurface->getRootItem());
        touchEvent.setTouchPoints(touchPoints);
        touchEvent.setTouchPointStates(touchPointStates);
    }

    if (touchType == QEvent::TouchBegin) {
        _touchBeginAccepted = QCoreApplication::sendEvent(_webSurface->getWindow(), &touchEvent);
    } else if (_touchBeginAccepted) {
        QCoreApplication::sendEvent(_webSurface->getWindow(), &touchEvent);
    }

    // If this was a release event, remove the point from the active touch points
    if (state == Qt::TouchPointReleased) {
        _activeTouchPoints.erase(event.getID());
    }
}

void WebEntityRenderer::setProxyWindow(QWindow* proxyWindow) {
    if (_webSurface) {
        _webSurface->setProxyWindow(proxyWindow);
    }
}

QObject* WebEntityRenderer::getEventHandler() {
    if (!_webSurface) {
        return nullptr;
    }
    return _webSurface->getEventHandler();
}

bool WebEntityRenderer::isTransparent() const {
    float fadeRatio = _isFading ? Interpolate::calculateFadeRatio(_fadeStartTime) : 1.0f;
    return fadeRatio < OPAQUE_ALPHA_THRESHOLD;
}

