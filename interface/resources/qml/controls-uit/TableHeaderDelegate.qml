import QtQuick 2.5
import QtQuick.Controls 1.4
import "../styles-uit"

Rectangle {
    property var parentTable: parent

    height: hifi.dimensions.tableHeaderHeight
    color: parentTable.isLightColorScheme ? hifi.colors.tableBackgroundLight : hifi.colors.tableBackgroundDark

    property alias titleText: titleText
    property alias titleSort: titleSort

    HifiConstants { id: hifi }


    RalewayRegular {
        id: titleText
        text: styleData.value
        size: hifi.fontSizes.tableHeading
        font.capitalization: Font.AllUppercase
        color: hifi.colors.baseGrayHighlight
        horizontalAlignment: (parentTable.centerHeaderText ? Text.AlignHCenter : Text.AlignLeft)
        anchors {
            left: parent.left
            leftMargin: hifi.dimensions.tablePadding
            right: parent.right
            rightMargin: hifi.dimensions.tablePadding
            verticalCenter: parent.verticalCenter
        }
    }

    HiFiGlyphs {
        id: titleSort
        text:  parentTable.sortIndicatorOrder === Qt.AscendingOrder ? hifi.glyphs.caratUp : hifi.glyphs.caratDn
        color: hifi.colors.baseGrayHighlight
        size: hifi.fontSizes.tableHeadingIcon
        anchors {
//            left: titleText.right
//            leftMargin: -hifi.fontSizes.tableHeadingIcon / 3 - (parentTable.centerHeaderText ? 3 : 0)
            right: parent.right
            //rightMargin: hifi.dimensions.tablePadding
            verticalCenter: titleText.verticalCenter
        }
        visible: parentTable.sortIndicatorVisible && (parentTable.sortIndicatorColumn === styleData.column)
    }

    Rectangle {
        width: 1
        anchors {
            left: parent.left
            top: parent.top
            topMargin: 1
            bottom: parent.bottom
            bottomMargin: 2
        }
        color: parentTable.isLightColorScheme ? hifi.colors.lightGrayText : hifi.colors.baseGrayHighlight
        visible: styleData.column > 0
    }

    Rectangle {
        height: 1
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        color: parentTable.isLightColorScheme ? hifi.colors.lightGrayText : hifi.colors.baseGrayHighlight
    }
}
