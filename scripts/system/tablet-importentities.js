"use strict";

//
//  goto.js
//  scripts/system/
//
//  Created by Dante Ruiz on 8 February 2017
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

Script.include("edit.js");

(function() { // BEGIN LOCAL_SCOPE

    var button;
    var buttonName = "Import\nEntitites";
    var toolBar = null;
    var tablet = null;

    function onClicked(){
        tablet.loadQMLSource("TabletImportEntities.qml");

//        var importURL = null;
//        var fullPath = Window.browse("Select Model to Import", "", "*.json");
//        if (fullPath) {
//            importURL = "file:///" + fullPath;
//        }
//        if (importURL) {
//            if (!isActive && (Entities.canRez() && Entities.canRezTmp())) {
//                toolBar.toggle();
//            }
//            Edit.importSVO(importURL);
//        }
    }
    if (Settings.getValue("HUDUIEnabled")) {
        toolBar = Toolbars.getToolbar("com.highfidelity.interface.toolbar.system");
        button = toolBar.addButton({
            objectName: buttonName,
            imageURL: Script.resolvePath("assets/images/tools/directory.svg"),
            visible: true,
            alpha: 0.9
        });
    } else {
        tablet = Tablet.getTablet("com.highfidelity.interface.tablet.system");
        button = tablet.addButton({
            icon: "icons/tablet-icons/goto-i.svg",
            activeIcon: "icons/tablet-icons/goto-a.svg",
            text: buttonName,
            sortOrder: 8
        });
    }
    
    button.clicked.connect(onClicked);
    
    Script.scriptEnding.connect(function () {
        button.clicked.disconnect(onClicked);
        if (tablet) {
            tablet.removeButton(button);
        }
        if (toolBar) {
            toolBar.removeButton(buttonName);
        }
   });
    
}()); // END LOCAL_SCOPE
