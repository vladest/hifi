//
//  FileTypeProfile.cpp
//  interface/src/networking
//
//  Created by Kunal Gosar on 2017-03-10.
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "FileTypeProfile.h"

#include "FileTypeRequestInterceptor.h"

#if !defined(Q_OS_ANDROID)
static const QString QML_WEB_ENGINE_STORAGE_NAME = "qmlWebEngine";

FileTypeProfile::FileTypeProfile(QObject* parent) :
    QQuickWebEngineProfile(parent)
{
    static const QString WEB_ENGINE_USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/60.0.3112.90 Safari/537.36 (HighFidelityInterface)";
    setHttpUserAgent(WEB_ENGINE_USER_AGENT);

    auto requestInterceptor = new FileTypeRequestInterceptor(this);
    setRequestInterceptor(requestInterceptor);
}
#endif
