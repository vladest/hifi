//
//  WebBrowser.qml
//
//
//  Created by Vlad Stelmahovsky on 06/22/2017
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import QtQuick 2.5
import QtQuick.Controls 1.5
import QtQuick.Layouts 1.3
import QtQuick.Controls.Styles 1.4

import QtWebEngine 1.2

import "../styles-uit"
import "../controls-uit" as HifiControls
import "../windows"

Rectangle {
    id: root;

    HifiConstants { id: hifi; }

    property var eventBridge;
    property string title: "";
    signal sendToScript(var message);

    color: hifi.colors.baseGray;

    // only show the title if loaded through a "loader"

    Column {
        //y: 16; // padding does not work
        spacing: 2
        width: parent.width;

        RowLayout {
            width: parent.width;
            height: 48
            HifiControls.GlyphButton {
                glyph: hifi.glyphs.backward;
                color: hifi.colors.primaryHighlight;
                anchors.verticalCenter: parent.verticalCenter;
                size: 38;
            }
            HifiControls.GlyphButton {
                glyph: hifi.glyphs.forward;
                color: hifi.colors.primaryHighlight;
                anchors.verticalCenter: parent.verticalCenter;
                width: hifi.dimensions.controlLineHeight
            }

            TextField {
                id: addressBar
                Image {
                    anchors.verticalCenter: addressBar.verticalCenter;
                    x: 5
                    z: 2
                    id: faviconImage
                    width: 16; height: 16
                    sourceSize: Qt.size(width, height)
                    source: webEngineView.icon
                }
                HifiControls.GlyphButton {
                    glyph: webEngineView.loading ? hifi.glyphs.closeSmall : hifi.glyphs.reloadSmall;
                    color: hifi.colors.primaryHighlight;
                    anchors.verticalCenter: parent.verticalCenter;
                    width: hifi.dimensions.controlLineHeight
                    z: 2
                    x: addressBar.width - 28
                }

                style: TextFieldStyle {
                    padding {
                        left: 26;
                        right: 26
                    }
                }
                focus: true
                Layout.fillWidth: true
                text: webEngineView.url
                onAccepted: webEngineView.url = text
            }
            HifiControls.GlyphButton {
                glyph: hifi.glyphs.unmuted;
                color: hifi.colors.primaryHighlight;
                anchors.verticalCenter: parent.verticalCenter;
                width: hifi.dimensions.controlLineHeight
            }

        }
        ProgressBar {
            width: parent.width;
            minimumValue: 0
            maximumValue: 100
            value: webEngineView.loadProgress
            height: 2
            id: loadProgress
        }

        WebEngineView {
            id: webEngineView
            focus: true
            url: "http://www.highfidelity.com"
            width: parent.width;
            height: root.height - loadProgress.height - 48 - 4

            onLinkHovered: {
//                if (hoveredUrl == "")
//                    resetStatusText.start();
//                else {
//                    resetStatusText.stop();
//                    statusText.text = hoveredUrl;
//                }
            }

            settings.autoLoadImages: true
            settings.javascriptEnabled: true
            settings.errorPageEnabled: true
            settings.pluginsEnabled: true
            settings.fullScreenSupportEnabled: false
            //from WebEngine 1.3
//            settings.autoLoadIconsForPage: false
//            settings.touchIconsEnabled: false

            onCertificateError: {
                error.defer();
                //sslDialog.enqueue(error);
            }

            Component.onCompleted: {
                console.log("Connecting JS messaging to Hifi Logging")
                // Ensure the JS from the web-engine makes it to our logging
                webEngineView.javaScriptConsoleMessage.connect(function(level, message, lineNumber, sourceID) {
                    console.log("Web Window JS message: " + sourceID + " " + lineNumber + " " +  message);
                });
            }

            onLoadingChanged: {
                // Required to support clicking on "hifi://" links
                if (WebEngineView.LoadStartedStatus == loadRequest.status) {
                    var url = loadRequest.url.toString();
                    if (urlHandler.canHandleUrl(url)) {
                        if (urlHandler.handleUrl(url)) {
                            webEngineView.stop();
                        }
                    }
                }
            }

            onNewViewRequested: {
                if (!request.userInitiated)
                    print("Warning: Blocked a popup window.");
                else if (request.destination == WebEngineView.NewViewInTab) {
//                    var tab = tabs.createEmptyTab(currentWebView.profile);
//                    tabs.currentIndex = tabs.count - 1;
//                    request.openIn(tab.item);
                } else if (request.destination == WebEngineView.NewViewInBackgroundTab) {
//                    var backgroundTab = tabs.createEmptyTab(currentWebView.profile);
//                    request.openIn(backgroundTab.item);
                } else if (request.destination == WebEngineView.NewViewInDialog) {
//                    var dialog = applicationRoot.createDialog(currentWebView.profile);
//                    request.openIn(dialog.currentWebView);
                } else {
//                    var window = applicationRoot.createWindow(currentWebView.profile);
//                    request.openIn(window.currentWebView);
                }
            }

            onRenderProcessTerminated: {
                var status = "";
                switch (terminationStatus) {
                case WebEngineView.NormalTerminationStatus:
                    status = "(normal exit)";
                    break;
                case WebEngineView.AbnormalTerminationStatus:
                    status = "(abnormal exit)";
                    break;
                case WebEngineView.CrashedTerminationStatus:
                    status = "(crashed)";
                    break;
                case WebEngineView.KilledTerminationStatus:
                    status = "(killed)";
                    break;
                }

                print("Render process exited with code " + exitCode + " " + status);
                reloadTimer.running = true;
            }

            onWindowCloseRequested: {
            }

            Timer {
                id: reloadTimer
                interval: 0
                running: false
                repeat: false
                onTriggered: webEngineView.reload()
            }
        }
    }
}
