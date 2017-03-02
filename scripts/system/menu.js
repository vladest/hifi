//
//  menu.js
//  scripts/system/
//
//  Created by Dante Ruiz  on 5 Jun 2017
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

var HOME_BUTTON_TEXTURE = "http://hifi-content.s3.amazonaws.com/alan/dev/tablet-with-home-button.fbx/tablet-with-home-button.fbm/button-root.png";
// var HOME_BUTTON_TEXTURE = Script.resourcesPath() + "meshes/tablet-with-home-button.fbx/tablet-with-home-button.fbm/button-root.png";

(function() {
    var tablet = Tablet.getTablet("com.highfidelity.interface.tablet.system");
    var button = tablet.addButton({
        icon: "icons/tablet-icons/menu-i.svg",
        activeIcon: "icons/tablet-icons/menu-a.svg",
        text: "MENU",
        sortOrder: 3
    });

    var shouldActivateButton = false;
    var onMenuScreen = false;

    function onClicked() {
        if (onMenuScreen) {
            // for toolbar-mode: go back to home screen, this will close the window.
            tablet.gotoHomeScreen();
        } else {
            var entity = HMD.tabletID;
            Entities.editEntity(entity, {textures: JSON.stringify({"tex.close": HOME_BUTTON_TEXTURE})});
            shouldActivateButton = true;
            tablet.gotoMenuScreen();
            onMenuScreen = true;
        }
    }

    function onScreenChanged(type, url) {
        // for toolbar mode: change button to active when window is first openend, false otherwise.
        button.editProperties({isActive: shouldActivateButton});
        shouldActivateButton = false;
        onMenuScreen = false;
    }

    button.clicked.connect(onClicked);
    tablet.screenChanged.connect(onScreenChanged);

    Script.scriptEnding.connect(function () {
        button.clicked.disconnect(onClicked);
        tablet.removeButton(button);
        tablet.screenChanged.disconnect(onScreenChanged);
    });
}());
