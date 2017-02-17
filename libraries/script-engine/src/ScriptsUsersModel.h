//
//  ScriptsUsersModel.h
//
//  Created by Vlad Stelmahovsky
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
#pragma once

#ifndef hifi_ScriptsUsersModel_h
#define hifi_ScriptsUsersModel_h

#include <QAbstractItemModel>
#include <DependencyManager.h>
#include <QJSValue>

struct User {
    User() : _index(0), _personal_mute(false), _mute(false),
    _ignore(false), _kick(false), _audio_level(0.0), _admin(false) {}
    qint32 _index;
    QString _session_id;
    QString _display_name;
    QString _user_name;
    bool _personal_mute;
    bool _ignore;
    bool _mute;
    bool _kick;
    qreal _audio_level;
    bool _admin;
};

class ScriptsUsersModel : public QAbstractItemModel, public Dependency {
    Q_OBJECT
    SINGLETON_DEPENDENCY
public:
    ScriptsUsersModel(QAbstractItemModel* parent = nullptr);
    ~ScriptsUsersModel();
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;

    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    enum Roles {
        DisplayNameRole = Qt::UserRole,
        UserNameRole,
        IndexRole,
        SessionIDRole,
        PersonalMuteRole,
        IgnoreRole,
        MuteRole,
        KickRole,
        AudioLevelRole,
        AdminRole
    };

    Q_INVOKABLE int setUsers(const QJSValue &value);
    Q_INVOKABLE bool setProperty(int index, const QByteArray &roleName, const QVariant &value);
    Q_INVOKABLE int findSessionIndex(const QString &id) const;

public slots:

protected:

private:
    QList<User> _usersList;
};

#endif // hifi_ScriptsUsersModel_h
