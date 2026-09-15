import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AtomStudio 1.0

ApplicationWindow {
    id: mainWindow
    property string persistentStatusMessage: ""

    visible: true
    width: 1400
    height: 900
    minimumWidth: 800
    minimumHeight: 600

    title: appName + " v" + appVersion

    color: "#1e1e1e"

    // Connection to handle file loading errors
    // Note: Structure updates are handled in C++ (FileController -> StructureModel -> OpenGLViewport)
    // because std::shared_ptr cannot pass through QML signals
    Connections {
        target: FileController
        function onLoadingStarted(filePath) {
            mainWindow.persistentStatusMessage = ""
        }

        function onStructureLoaded(structure) {
            mainWindow.persistentStatusMessage = ""
        }

        function onLoadFailed(error) {
            mainWindow.persistentStatusMessage = "Error: " + error
        }

        function onStructureExportStarted(filePath) {
            mainWindow.persistentStatusMessage = qsTr("Exporting structure...")
        }

        function onStructureExported(filePath) {
            mainWindow.persistentStatusMessage = qsTr("Structure exported to %1").arg(filePath)
        }

        function onStructureExportFailed(error) {
            mainWindow.persistentStatusMessage = qsTr("Export failed: %1").arg(error)
            structureExportError.message = error
            structureExportError.open()
        }
    }

    Dialog {
        id: structureExportError
        property string message: ""
        title: qsTr("Structure Export Failed")
        anchors.centerIn: parent
        width: Math.min(mainWindow.width - 40, 440)
        modal: true
        standardButtons: Dialog.Ok
        contentItem: Label {
            text: structureExportError.message
            wrapMode: Text.Wrap
        }
    }

    // Main layout
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Main content area with SplitView
        SplitView {
            id: mainSplitView
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            // Visualization viewport (left/main area)
            ViewportPanel {
                id: viewportPanel
                SplitView.fillWidth: true
                SplitView.minimumWidth: 400
            }

            // Properties sidebar (right)
            Sidebar {
                id: sidebar
                onOpenInteractiveShell: PythonShellController.openWindow()
                onOpenShellTutorial: PythonShellController.openTutorial()
                onOpenPythonPackages: PythonShellController.openPackages()
                onOpenEnvironmentTerminal: PythonShellController.openEnvironmentTerminal()
                viewport: viewportPanel.viewport
                SplitView.preferredWidth: 320
                SplitView.minimumWidth: 200
                SplitView.maximumWidth: 600
            }
        }
    }

    // Status bar at the bottom
    footer: Rectangle {
        height: 30

        gradient: Gradient {
            GradientStop { position: 0.0; color: sidebar ? sidebar.panelBgTop : "#fbfbfc" }
            GradientStop { position: 1.0; color: sidebar ? sidebar.panelBgBottom : "#efeff2" }
        }

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: sidebar ? sidebar.borderSoft : "#c7c9d1"
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            Label {
                id: statusLabel
                text: FileController.isLoading
                    ? FileController.loadStatus
                    : (viewportPanel.viewport && viewportPanel.viewport.hoverStatus.length > 0
                        ? viewportPanel.viewport.hoverStatus
                        : (sidebar.statusHint.length > 0
                            ? sidebar.statusHint
                            : (mainWindow.persistentStatusMessage.length > 0
                                ? mainWindow.persistentStatusMessage
                                : "Ready")))
                color: sidebar ? sidebar.textBody : "#33353c"
                font.pixelSize: 12
                font.weight: Font.Medium
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            // Loading progress
            ProgressBar {
                visible: FileController.isLoading
                value: FileController.loadProgress
                Layout.preferredWidth: 120

                background: Rectangle {
                    radius: 3
                    color: sidebar ? sidebar.inputFillAlt : "#f3f4f7"
                    border.width: 1
                    border.color: sidebar ? sidebar.borderSoft : "#c7c9d1"
                }

                contentItem: Item {
                    implicitHeight: 6

                    Rectangle {
                        width: parent.width * parent.ProgressBar.visualPosition
                        height: parent.height
                        radius: 3
                        color: sidebar ? sidebar.connectorColor : "#6e727d"
                    }
                }
            }
        }
    }
}
