"use strict";

//
//  webbrowser.js
//
//  Created by Vlad Stelmahovsky on 20 Jun 2017
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
/* eslint indent: ["error", 4, { "outerIIFEBody": 0 }] */

(function() { // BEGIN LOCAL_SCOPE

var TABLET_BUTTON_NAME = "WEB";
var HOME_BUTTON_TEXTURE = "http://hifi-content.s3.amazonaws.com/alan/dev/tablet-with-home-button.fbx/tablet-with-home-button.fbm/button-root.png";

var ICONS = {
    icon: "icons/tablet-icons/web-i.svg",
    activeIcon: "icons/tablet-icons/web-i.svg"
};

var shouldActivateButton = false;
var onWebBrowserScreen = false;

function onClicked() {
    if (onWebBrowserScreen) {
        // for toolbar-mode: go back to home screen, this will close the window.
        tablet.gotoHomeScreen();
    } else {
        var entity = HMD.tabletID;
        Entities.editEntity(entity, { textures: JSON.stringify({ "tex.close": HOME_BUTTON_TEXTURE }) });
        shouldActivateButton = true;
        tablet.loadQMLSource("../WebBrowser.qml");
        onWebBrowserScreen = true;
    }
}

function onScreenChanged(type, url) {
    // for toolbar mode: change button to active when window is first openend, false otherwise.
    button.editProperties({isActive: shouldActivateButton});
    shouldActivateButton = false;
    onWebBrowserScreen = false;
}

var tablet = Tablet.getTablet("com.highfidelity.interface.tablet.system");
var button = tablet.addButton({
    icon: ICONS.icon,
    activeIcon: ICONS.activeIcon,
    text: TABLET_BUTTON_NAME,
    sortOrder: 1
});


button.clicked.connect(onClicked);
tablet.screenChanged.connect(onScreenChanged);

Script.scriptEnding.connect(function () {
    if (onWebBrowserScreen) {
        tablet.gotoHomeScreen();
    }
    button.clicked.disconnect(onClicked);
    tablet.screenChanged.disconnect(onScreenChanged);
    tablet.removeButton(button);
});

}()); // END LOCAL_SCOPE
