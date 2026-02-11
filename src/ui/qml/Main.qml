import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AtomStudio 1.0

ApplicationWindow {
    id: mainWindow

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
        function onLoadFailed(error) {
            statusLabel.text = "Error: " + error
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
                viewport: viewportPanel.viewport
                SplitView.preferredWidth: 320
                SplitView.minimumWidth: 200
                SplitView.maximumWidth: 600
            }
        }
    }

    // Status bar at the bottom
    footer: Rectangle {
        height: 24
        color: "#252526"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 20

            Label {
                id: statusLabel
                text: FileController.isLoading ? FileController.loadStatus : "Ready"
                color: "#cccccc"
                font.pixelSize: 11
            }

            // Loading progress
            ProgressBar {
                visible: FileController.isLoading
                value: FileController.loadProgress
                Layout.preferredWidth: 100
            }

            Item { Layout.fillWidth: true }

            Label {
                text: StructureModel.hasStructure ? StructureModel.fileName : "No file loaded"
                color: "#808080"
                font.pixelSize: 11
            }
        }
    }
}
