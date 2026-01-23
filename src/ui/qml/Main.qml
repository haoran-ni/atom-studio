import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: mainWindow

    visible: true
    width: 1400
    height: 900
    minimumWidth: 800
    minimumHeight: 600

    title: appName + " v" + appVersion

    color: "#1e1e1e"

    // Main layout
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header bar at the top
        HeaderBar {
            id: headerBar
            Layout.fillWidth: true
            Layout.preferredHeight: 40
        }

        // Separator line
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: "#3c3c3c"
        }

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
                text: "Ready"
                color: "#cccccc"
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            Label {
                text: "No file loaded"
                color: "#808080"
                font.pixelSize: 11
            }
        }
    }
}
