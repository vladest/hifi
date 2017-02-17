//
//  ScriptsUsersModelFilter.h
//
//  Created by Vlad Stelmahovsky
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#pragma once

#ifndef hifi_ScriptsUsersModelFilter_h
#define hifi_ScriptsUsersModelFilter_h

#include "ScriptsUsersModel.h"
#include <QSortFilterProxyModel>
#include <DependencyManager.h>

class ScriptsUsersModelFilter : public QSortFilterProxyModel, public Dependency {
    Q_OBJECT
    SINGLETON_DEPENDENCY
public:
    ScriptsUsersModelFilter(QObject *parent = NULL);
protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
};

#endif // hifi_ScriptsModelFilter_h
