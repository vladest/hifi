//
//  WebView.qml
//
//  Created by Bradley Austin Davis on 12 Jan 2016
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import QtQuick 2.7
import QtWebEngine 1.5

WebEngineView {
    id: root

    Component.onCompleted: {
        console.log("Connecting JS messaging to Hifi Logging")
        // Ensure the JS from the web-engine makes it to our logging
        root.javaScriptConsoleMessage.connect(function(level, message, lineNumber, sourceID) {
            console.log("Web Window JS message: " + sourceID + " " + lineNumber + " " +  message);
        });
    }

    onLoadingChanged: {
        // Required to support clicking on "hifi://" links
        if (WebEngineView.LoadStartedStatus == loadRequest.status) {
            var url = loadRequest.url.toString();
            if (urlHandler.canHandleUrl(url)) {
                if (urlHandler.handleUrl(url)) {
                    root.stop();
                }
            }
        }
    }
    AnimatedImage {
        //anchoring doesnt works here when changing content size
        x: root.width/2 - width/2
        y: root.height/2 - height/2
        source: "../../icons/loader-snake-64-w.gif"
        visible: (root.loading && root.loadProgress < 99.9) && /^(http.*|)$/i.test(root.url.toString())
        playing: visible
        z: 10000
    }

}
