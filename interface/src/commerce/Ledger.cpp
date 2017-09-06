//
//  Ledger.cpp
//  interface/src/commerce
//
//  Created by Howard Stearns on 8/4/17.
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QJsonObject>
#include <QJsonArray>
#include <QTimeZone>
#include <QJsonDocument>
#include "AccountManager.h"
#include "Wallet.h"
#include "Ledger.h"
#include "CommerceLogging.h"

// inventory answers {status: 'success', data: {assets: [{id: "guid", title: "name", preview: "url"}....]}}
// balance answers {status: 'success', data: {balance: integer}}
// buy and receive_at answer {status: 'success'}

QJsonObject Ledger::apiResponse(const QString& label, QNetworkReply& reply) {
    QByteArray response = reply.readAll();
    QJsonObject data = QJsonDocument::fromJson(response).object();
    qInfo(commerce) << label << "response" << QJsonDocument(data).toJson(QJsonDocument::Compact);
    return data;
}
// Non-200 responses are not json:
QJsonObject Ledger::failResponse(const QString& label, QNetworkReply& reply) {
    QString response = reply.readAll();
    qWarning(commerce) << "FAILED" << label << response;
    QJsonObject result
    {
        { "status", "fail" },
        { "message", response }
    };
    return result;
}
#define ApiHandler(NAME) void Ledger::NAME##Success(QNetworkReply& reply) { emit NAME##Result(apiResponse(#NAME, reply)); }
#define FailHandler(NAME) void Ledger::NAME##Failure(QNetworkReply& reply) { emit NAME##Result(failResponse(#NAME, reply)); }
#define Handler(NAME) ApiHandler(NAME) FailHandler(NAME)
Handler(buy)
Handler(receiveAt)
Handler(balance)
Handler(inventory)

void Ledger::send(const QString& endpoint, const QString& success, const QString& fail, QNetworkAccessManager::Operation method, QJsonObject request) {
    auto accountManager = DependencyManager::get<AccountManager>();
    const QString URL = "/api/v1/commerce/";
    JSONCallbackParameters callbackParams(this, success, this, fail);
    qCInfo(commerce) << "Sending" << endpoint << QJsonDocument(request).toJson(QJsonDocument::Compact);
    accountManager->sendRequest(URL + endpoint,
        AccountManagerAuth::Required,
        method,
        callbackParams,
        QJsonDocument(request).toJson());
}

void Ledger::signedSend(const QString& propertyName, const QByteArray& text, const QString& key, const QString& endpoint, const QString& success, const QString& fail) {
    auto wallet = DependencyManager::get<Wallet>();
    QString signature = key.isEmpty() ? "" : wallet->signWithKey(text, key);
    QJsonObject request;
    request[propertyName] = QString(text);
    request["signature"] = signature;
    send(endpoint, success, fail, QNetworkAccessManager::PutOperation, request);
}

void Ledger::keysQuery(const QString& endpoint, const QString& success, const QString& fail) {
    auto wallet = DependencyManager::get<Wallet>();
    QJsonObject request;
    request["public_keys"] = QJsonArray::fromStringList(wallet->listPublicKeys());
    send(endpoint, success, fail, QNetworkAccessManager::PostOperation, request);
}

void Ledger::buy(const QString& hfc_key, int cost, const QString& asset_id, const QString& inventory_key, const QString& buyerUsername) {
    QJsonObject transaction;
    transaction["hfc_key"] = hfc_key;
    transaction["cost"] = cost;
    transaction["asset_id"] = asset_id;
    transaction["inventory_key"] = inventory_key;
    transaction["inventory_buyer_username"] = buyerUsername;
    QJsonDocument transactionDoc{ transaction };
    auto transactionString = transactionDoc.toJson(QJsonDocument::Compact);
    signedSend("transaction", transactionString, hfc_key, "buy", "buySuccess", "buyFailure");
}

bool Ledger::receiveAt(const QString& hfc_key, const QString& old_key) {
    auto accountManager = DependencyManager::get<AccountManager>();
    if (!accountManager->isLoggedIn()) {
        qCWarning(commerce) << "Cannot set receiveAt when not logged in.";
        QJsonObject result{ { "status", "fail" }, { "message", "Not logged in" } };
        emit receiveAtResult(result);
        return false; // We know right away that we will fail, so tell the caller.
    }

    signedSend("public_key", hfc_key.toUtf8(), old_key, "receive_at", "receiveAtSuccess", "receiveAtFailure");
    return true; // Note that there may still be an asynchronous signal of failure that callers might be interested in.
}

void Ledger::balance(const QStringList& keys) {
    keysQuery("balance", "balanceSuccess", "balanceFailure");
}

void Ledger::inventory(const QStringList& keys) {
    keysQuery("inventory", "inventorySuccess", "inventoryFailure");
}

QString nameFromKey(const QString& key, const QStringList& publicKeys) {
    if (key.isNull() || key.isEmpty()) {
        return "<b>Marketplace</b>";
    }
    if (publicKeys.contains(key)) {
        return "You";
    }
    return "<b>Someone</b>";
}

void Ledger::historySuccess(QNetworkReply& reply) {
    // here we send a historyResult with some extra stuff in it
    // Namely, the styled text we'd like to show.  The issue is the
    // QML cannot do that easily since it doesn't know what the wallet
    // public key(s) are.  Let's keep it that way
    QByteArray response = reply.readAll();
    QJsonObject data = QJsonDocument::fromJson(response).object();

    // we will need the array of public keys from the wallet
    auto wallet = DependencyManager::get<Wallet>();
    auto keys = wallet->listPublicKeys();

    // now we need to loop through the transactions and add fancy text...
    auto historyArray = data.find("data").value().toObject().find("history").value().toArray();
    QJsonArray newHistoryArray;

    // TODO: do this with 0 copies if possible
    for(auto it = historyArray.begin(); it != historyArray.end(); it++) {
        auto valueObject = (*it).toObject();
        QString from = nameFromKey(valueObject["sender_key"].toString(), keys);
        QString to = nameFromKey(valueObject["recipient_key"].toString(), keys);
        // turns out on my machine, toLocalTime convert to some weird timezone, yet the
        // systemTimeZone is correct.  To avoid a strange bug with other's systems too, lets
        // be explicit
#ifdef Q_OS_MAC
        QDateTime createdAt = QDateTime::fromTime_t(valueObject["created_at"].toInt(), Qt::UTC);
#else
        QDateTime createdAt = QDateTime::fromSecsSinceEpoch(valueObject["created_at"].toInt(), Qt::UTC);
#endif
        QDateTime localCreatedAt = createdAt.toTimeZone(QTimeZone::systemTimeZone());
        valueObject["text"] = QString("%1 sent %2 <b>%3 %4</b> on %5 with message \"%6\"").
            arg(from, to, QString::number(valueObject["quantity"].toInt()), valueObject["asset_title"].toString(), localCreatedAt.toString(Qt::SystemLocaleShortDate), valueObject["message"].toString());
        newHistoryArray.push_back(valueObject);
    }
    // now copy the rest of the json -- this is inefficient
    // TODO: try to do this without making copies
    QJsonObject newData;
    newData["status"] = "success";
    QJsonObject newDataData;
    newDataData["history"] = newHistoryArray;
    newData["data"] = newDataData;
    emit historyResult(newData);
}

void Ledger::historyFailure(QNetworkReply& reply) {
    failResponse("history", reply);
}

void Ledger::history(const QStringList& keys) {
    keysQuery("history", "historySuccess", "historyFailure");
}

// The api/failResponse is called just for the side effect of logging.
void Ledger::resetSuccess(QNetworkReply& reply) { apiResponse("reset", reply); }
void Ledger::resetFailure(QNetworkReply& reply) { failResponse("reset", reply); }
void Ledger::reset() {
    send("reset_user_hfc_account", "resetSuccess", "resetFailure", QNetworkAccessManager::PutOperation, QJsonObject());
}
