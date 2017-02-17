//
//  ScriptsUsersModelFilter.cpp
//
//  Created by Vlad Stelmahovsky
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ScriptsUsersModelFilter.h"

ScriptsUsersModelFilter::ScriptsUsersModelFilter(QObject *parent) :
    QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    setSortRole(ScriptsUsersModel::DisplayNameRole);
}

bool ScriptsUsersModelFilter::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    ScriptsUsersModel* scriptsUsersModel = static_cast<ScriptsUsersModel*>(sourceModel());
    QString userRight = scriptsUsersModel->data(right, ScriptsUsersModel::DisplayNameRole).toString();
    QString userLeft = scriptsUsersModel->data(left, ScriptsUsersModel::DisplayNameRole).toString();
    qDebug() << "comparing" << userLeft << userRight;
    return  QString::localeAwareCompare(userLeft, userRight) < 0;;
}

bool ScriptsUsersModelFilter::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    return true;
}
