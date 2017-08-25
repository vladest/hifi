//
//  QmlCommerce.h
//  interface/src/commerce
//
//  Guard for safe use of Commerce (Wallet, Ledger) by authorized QML.
//
//  Created by Howard Stearns on 8/4/17.
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#pragma once
#ifndef hifi_QmlCommerce_h
#define hifi_QmlCommerce_h

#include <QJsonObject>
#include <OffscreenQmlDialog.h>

#include <QPixmap>

class QmlCommerce : public OffscreenQmlDialog {
    Q_OBJECT
    HIFI_QML_DECL

public:
    QmlCommerce(QQuickItem* parent = nullptr);

signals:
    void buyResult(QJsonObject result);
    // Balance and Inventory are NOT properties, because QML can't change them (without risk of failure), and
    // because we can't scalably know of out-of-band changes (e.g., another machine interacting with the block chain).
    void balanceResult(QJsonObject result);
    void inventoryResult(QJsonObject result);
    void securityImageResult(bool exists);
    void loginStatusResult(bool isLoggedIn);
    void passphraseSetupStatusResult(bool passphraseIsSetup);
    void keyFilePathResult(const QString& path);

protected:
    Q_INVOKABLE void buy(const QString& assetId, int cost, const QString& buyerUsername = "");
    Q_INVOKABLE void balance();
    Q_INVOKABLE void inventory();
    Q_INVOKABLE void chooseSecurityImage(const QString& imageFile);
    Q_INVOKABLE void getSecurityImage();
    Q_INVOKABLE void getLoginStatus();
    Q_INVOKABLE void setPassphrase(const QString& passphrase);
    Q_INVOKABLE void getPassphraseSetupStatus();
    Q_INVOKABLE void getKeyFilePath();
};

#endif // hifi_QmlCommerce_h
