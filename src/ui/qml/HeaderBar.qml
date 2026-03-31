import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AtomStudio 1.0

Rectangle {
    id: headerBar

    color: "#2d2d2d"

    AppMenuActions { id: appActions }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 10
        spacing: 0

        // Menu bar
        MenuBar {
            id: menuBar
            Layout.fillHeight: true

            background: Rectangle {
                color: "transparent"
            }

            // File menu
            Menu {
                title: qsTr("File")

                MenuItem { action: appActions.fileOpen }
                MenuItem { action: appActions.fileOpenRecent }
                MenuSeparator {}
                MenuItem { action: appActions.fileSaveImage }
                MenuItem { action: appActions.fileExport }
                MenuSeparator {}
                MenuItem { action: appActions.fileQuit }
            }

            // Edit menu
            Menu {
                title: qsTr("Edit")

                MenuItem { action: appActions.editUndo }
                MenuItem { action: appActions.editRedo }
                MenuSeparator {}
                MenuItem { action: appActions.editSelectAll }
                MenuItem { action: appActions.editDeselectAll }
                MenuSeparator {}
                MenuItem { action: appActions.editPreferences }
            }

            // View menu
            Menu {
                title: qsTr("View")

                Action {
                    text: qsTr("Reset Camera")
                    onTriggered: {
                        if (mainWindow.viewportPanel && mainWindow.viewportPanel.viewport) {
                            mainWindow.viewportPanel.viewport.resetCamera()
                        }
                    }
                }

                Action {
                    text: qsTr("Fit to View")
                    shortcut: "F"
                    onTriggered: {
                        if (mainWindow.viewportPanel && mainWindow.viewportPanel.viewport) {
                            mainWindow.viewportPanel.viewport.fitToView()
                        }
                    }
                }

                MenuSeparator {}

                Menu {
                    title: qsTr("Projection")

                    Action {
                        text: qsTr("Perspective")
                        checkable: true
                        checked: true
                    }

                    Action {
                        text: qsTr("Orthographic")
                        checkable: true
                        checked: false
                    }
                }

                MenuSeparator {}

                Action {
                    text: qsTr("Toggle Sidebar")
                    shortcut: "Ctrl+B"
                    checkable: true
                    checked: true
                    onTriggered: console.log("Toggle sidebar:", checked)
                }

                Action {
                    text: qsTr("Fullscreen")
                    shortcut: StandardKey.FullScreen
                    checkable: true
                    onTriggered: {
                        if (checked) {
                            mainWindow.showFullScreen()
                        } else {
                            mainWindow.showNormal()
                        }
                    }
                }
            }

            // Render menu
            Menu {
                title: qsTr("Render")

                Action {
                    text: qsTr("Interactive (Raster)")
                    checkable: true
                    checked: true
                }

                Action {
                    text: qsTr("Ray Tracing")
                    checkable: true
                    checked: false
                }

                MenuSeparator {}

                Action {
                    text: qsTr("Render Settings...")
                    onTriggered: console.log("Render settings triggered")
                }
            }

            // Help menu
            Menu {
                title: qsTr("Help")

                Action {
                    text: qsTr("Documentation")
                    onTriggered: console.log("Documentation triggered")
                }

                Action {
                    text: qsTr("Keyboard Shortcuts")
                    onTriggered: console.log("Shortcuts triggered")
                }

                MenuSeparator {}

                Action {
                    text: qsTr("About ATOM-STUDIO")
                    onTriggered: console.log("About triggered")
                }
            }
        }

        // Spacer
        Item {
            Layout.fillWidth: true
        }

        // Right side toolbar buttons (placeholder)
        RowLayout {
            spacing: 5

            ToolButton {
                text: "R"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Toggle Ray Tracing")
                onClicked: console.log("Toggle ray tracing")
            }
        }
    }
}
