//
//  TabletAddressDialog.qml
//
//  Created by Dante Ruiz on 2016/07/16
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import Hifi 1.0
import QtQuick 2.4
import QtGraphicalEffects 1.0
import "../../styles"
import "tabletWindows"
import "../../styles-uit" as HifiStyles
import "../../controls-uit" as HifiControls

Item {
    id: root
    HifiConstants { id: hifi }
    HifiStyles.HifiConstants { id: hifiStyleConstants }
    
    width: parent.width
    height: parent.height

    TabletFileDialog {
        anchors.fill: parent
        anchors.margins: 5
        //title: qsTr("Select Model to Import")
        nameFilters: ["*.json"]
        selectDirectory: false
        onSelectedFile: {
            console.log("selected file:", file)
        }
        onCanceled: root.parent.parent.pop()
    }
}
