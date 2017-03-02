import QtQuick 2.5
import QtQuick.Controls 1.0
import QtWebEngine 1.1
import QtWebChannel 1.0
import QtQuick.Controls.Styles 1.4
import "../../controls"
import "../toolbars"
import HFWebEngineProfile 1.0

StackView {
    id: editRoot
    objectName: "stack"
    initialItem: editBasePage

    property var eventBridge;
    signal sendToScript(var message);

    function pushSource(path) {
        editRoot.push(Qt.resolvedUrl(path));
        editRoot.currentItem.eventBridge = editRoot.eventBridge;
        editRoot.currentItem.sendToScript.connect(editRoot.sendToScript);
    }

    function popSource() {
        editRoot.pop();
    }

    Component {
        id: editBasePage
        TabView {
            id: editTabView
            // anchors.fill: parent
            height: 60

            Tab {
            title: "Entity List"
                active: true
                enabled: true
                property string originalUrl: ""

                Rectangle { 
                    color: "#404040"

                    Text {
                        color: "#ffffff"
                        text: "Choose an Entity Type to Create:"
                        font.pixelSize: 14
                        font.bold: true
                        anchors.top: parent.top
                        anchors.topMargin: 28
                        anchors.left: parent.left
                        anchors.leftMargin: 28
                    }

                    Flow {
                        id: createEntitiesFlow
                        spacing: 35
                        anchors.right: parent.right
                        anchors.rightMargin: 55
                        anchors.left: parent.left
                        anchors.leftMargin: 55
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 30
                        anchors.top: parent.top
                        anchors.topMargin: 70

                        NewEntityButton {
                            icon: "icons/assets-01.svg"
                            text: "ASSETS"
                            onClicked: {
                                editRoot.sendToScript({
                                    method: "newEntityButtonClicked", params: { buttonName: "openAssetBrowserButton" }
                                });
                            }
                        }

                        NewEntityButton {
                            icon: "icons/create-icons/94-model-01.svg"
                            text: "MODEL"
                            onClicked: {
                                editRoot.sendToScript({
                                    method: "newEntityButtonClicked", params: { buttonName: "newModelButton" }
                                });
                            }
                        }

                        NewEntityButton {
                            icon: "icons/create-icons/21-cube-01.svg"
                            text: "CUBE"
                            onClicked: {
                                editRoot.sendToScript({
                                    method: "newEntityButtonClicked", params: { buttonName: "newCubeButton" }
                                });
                            }
                        }

                        NewEntityButton {
                            icon: "icons/create-icons/22-sphere-01.svg"
                            text: "SPHERE"
                            onClicked: {
                                editRoot.sendToScript({
                                    method: "newEntityButtonClicked", params: { buttonName: "newSphereButton" }
                                });
                            }
                        }

                        NewEntityButton {
                            icon: "icons/create-icons/24-light-01.svg"
                            text: "LIGHT"
                            onClicked: {
                                editRoot.sendToScript({
                                    method: "newEntityButtonClicked", params: { buttonName: "newLightButton" }
                                });
                            }
                        }

                        NewEntityButton {
                            icon: "icons/create-icons/20-text-01.svg"
                            text: "TEXT"
                            onClicked: {
                                editRoot.sendToScript({
                                    method: "newEntityButtonClicked", params: { buttonName: "newTextButton" }
                                });
                            }
                        }

                        NewEntityButton {
                            icon: "icons/create-icons/25-web-1-01.svg"
                            text: "WEB"
                            onClicked: {
                                editRoot.sendToScript({
                                    method: "newEntityButtonClicked", params: { buttonName: "newWebButton" }
                                });
                            }
                        }

                        NewEntityButton {
                            icon: "icons/create-icons/23-zone-01.svg"
                            text: "ZONE"
                            onClicked: {
                                editRoot.sendToScript({
                                    method: "newEntityButtonClicked", params: { buttonName: "newZoneButton" }
                                });
                            }
                        }

                        NewEntityButton {
                            icon: "icons/create-icons/90-particles-01.svg"
                            text: "PARTICLE"
                            onClicked: {
                                editRoot.sendToScript({
                                    method: "newEntityButtonClicked", params: { buttonName: "newParticleButton" }
                                });
                            }
                        }
                    }



                }
            }

            Tab {
                title: "LIST"
                active: true
                enabled: true
                property string originalUrl: ""
                WebView {
                    id: entityListToolWebView
                    url: "../../../../../scripts/system/html/entityList.html"
                    eventBridge: editRoot.eventBridge
                    anchors.fill: parent
                    enabled: true
                }
            }

            Tab {
            title: "Entity Properties"
                active: true
                enabled: true
                property string originalUrl: ""

                WebView {
                    id: entityPropertiesWebView
                    url: "../../../../../scripts/system/html/entityProperties.html"
                    eventBridge: editRoot.eventBridge
                    anchors.fill: parent
                    enabled: true
                }
            }

            Tab {
            title: "Grid"
                active: true
                enabled: true
                property string originalUrl: ""

                WebView {
                    id: gridControlsWebView
                    url: "../../../../../scripts/system/html/gridControls.html"
                    eventBridge: editRoot.eventBridge
                    anchors.fill: parent
                    enabled: true
                }
            }

            Tab {
                title: "P"
                active: true
                enabled: true
                property string originalUrl: ""

                WebView {
                    id: particleExplorerWebView
                    url: "../../../../../scripts/system/particle_explorer/particleExplorer.html"
                    eventBridge: editRoot.eventBridge
                    anchors.fill: parent
                    enabled: true
                }
            }


            style: TabViewStyle {
                frameOverlap: 1
                tab: Rectangle {
                    color: styleData.selected ? "#404040" :"black"
                    implicitWidth: text.width + 42
                    implicitHeight: 40
                    Text {
                        id: text
                        anchors.centerIn: parent
                        text: styleData.title
                        font.pixelSize: 16
                        font.bold: true
                        color: styleData.selected ? "white" : "white"
                    }
                }
                tabBar: Rectangle {
                    color: "black"
                    anchors.right: parent.right
                    anchors.rightMargin: 0
                    anchors.left: parent.left
                    anchors.leftMargin: 0
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 0
                    anchors.top: parent.top
                    anchors.topMargin: 0
                }
            }
        }
    }
}
