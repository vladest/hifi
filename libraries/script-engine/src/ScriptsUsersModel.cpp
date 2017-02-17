//
//  ScriptsUsersModel.cpp
//
//  Created by Vlad Stelmahovsky
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ScriptsUsersModel.h"
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(scriptsusersmodel, "hifi.scriptsusersmodel")

ScriptsUsersModel::ScriptsUsersModel(QAbstractItemModel* parent) :
    QAbstractItemModel(parent)
{}

ScriptsUsersModel::~ScriptsUsersModel() {
    _usersList.clear();
}

bool ScriptsUsersModel::setProperty(int row, const QByteArray &roleName, const QVariant &value) {
    //sanity
    if (row < 0 || row >= _usersList.size())
        return false;
    int roleNum = roleNames().key(roleName, -1);
    if (roleNum == -1)
        return false;
    QModelIndex index = createIndex(row, 0);

    if (roleNum == DisplayNameRole) {
        _usersList[row]._display_name = value.toString();
    } else if (roleNum == UserNameRole) {
        _usersList[row]._user_name = value.toString();
    } else if (roleNum == IndexRole) {
        _usersList[row]._index = value.toInt();
    } else if (roleNum == PersonalMuteRole) {
        _usersList[row]._personal_mute = value.toBool();
    } else if (roleNum == IgnoreRole) {
        _usersList[row]._ignore = value.toBool();
    } else if (roleNum == MuteRole) {
        _usersList[row]._mute = value.toBool();
    } else if (roleNum == KickRole) {
        _usersList[row]._kick = value.toBool();
    } else if (roleNum == AudioLevelRole) {
        _usersList[row]._audio_level = (qreal)value.toDouble();
    } else if (roleNum == AdminRole) {
        _usersList[row]._admin = value.toBool();
    } else if (roleNum == SessionIDRole) {
        _usersList[row]._session_id = value.toString();
    }
    emit dataChanged(index, index);
    return true;
}

int ScriptsUsersModel::findSessionIndex(const QString &id) const {
    for (int i = 0; i < _usersList.size(); ++i) {
        if (_usersList.at(i)._session_id == id)
            return i;
    }
    return -1;
}

QVariant ScriptsUsersModel::data(const QModelIndex& index, int role) const {
    //sanity
    if (!index.isValid() || index.row() >= _usersList.size())
        return QVariant();

    const User &user = _usersList.at(index.row());
    if (role == Qt::DisplayRole || role == DisplayNameRole) {
        return user._display_name;
    } else if (role == UserNameRole) {
        return user._user_name;
    } else if (role == IndexRole) {
        return user._index;
    } else if (role == PersonalMuteRole) {
        return user._personal_mute;
    } else if (role == IgnoreRole) {
        return user._ignore;
    } else if (role == MuteRole) {
        return user._mute;
    } else if (role == KickRole) {
        return user._kick;
    } else if (role == AudioLevelRole) {
        return user._audio_level;
    } else if (role == AdminRole) {
        return user._admin;
    } else if (role == SessionIDRole) {
        return user._session_id;
    }
    return QVariant();
}

int ScriptsUsersModel::rowCount(const QModelIndex& parent) const {
    return _usersList.count();
}

QHash<int, QByteArray> ScriptsUsersModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles.insert(DisplayNameRole, "displayName");
    roles.insert(UserNameRole, "userName");
    roles.insert(IndexRole, "index");
    roles.insert(PersonalMuteRole, "personalMute");
    roles.insert(IgnoreRole, "ignore");
    roles.insert(MuteRole, "mute");
    roles.insert(KickRole, "kick");
    roles.insert(AudioLevelRole, "audioLevel");
    roles.insert(AdminRole, "admin");
    roles.insert(SessionIDRole, "sessionId");
    return roles;
}

User parseUser(const QJSValue &value, bool *ok)
{
    User _u;

    if (value.isObject()) {
        if (value.hasProperty(QStringLiteral("displayName")))
            _u._display_name = value.property(QStringLiteral("displayName")).toString();
        if (value.hasProperty(QStringLiteral("userName")))
            _u._user_name = value.property(QStringLiteral("userName")).toString();
        if (value.hasProperty(QStringLiteral("sessionId")))
            _u._session_id = value.property(QStringLiteral("sessionId")).toString();
        if (value.hasProperty(QStringLiteral("audioLevel")))
            _u._audio_level = value.property(QStringLiteral("audioLevel")).toNumber();
        if (value.hasProperty(QStringLiteral("admin")))
            _u._admin = value.property(QStringLiteral("admin")).toBool();
        if (value.hasProperty(QStringLiteral("personalMute")))
            _u._personal_mute = value.property(QStringLiteral("personalMute")).toBool();
        if (value.hasProperty(QStringLiteral("ignore")))
            _u._ignore = value.property(QStringLiteral("ignore")).toBool();

        if (ok)
            *ok = true;
    }
    return _u;
}

int ScriptsUsersModel::setUsers(const QJSValue &value)
{
    if (!value.isArray()) {
        qCWarning(scriptsusersmodel) << "JS value is not array";
        return 0;
    }

    beginResetModel();
    _usersList.clear();

    quint32 length = value.property(QStringLiteral("length")).toUInt();
    for (quint32 i = 0; i < length; ++i) {
        bool ok;
        User u = parseUser(value.property(i), &ok);
        u._index = i;

        if (!ok) {
            qCWarning(scriptsusersmodel) << "Error parsing users for model at index" << i;
        } else {
            _usersList.append(u);
        }
    }
    endResetModel();
    return _usersList.size();
}

QModelIndex ScriptsUsersModel::index(int row, int column, const QModelIndex &parent) const
{
    return createIndex(row, column);
}

QModelIndex ScriptsUsersModel::parent(const QModelIndex &child) const
{
    return QModelIndex();
}

int ScriptsUsersModel::columnCount(const QModelIndex &parent) const
{
    return 1;
}
