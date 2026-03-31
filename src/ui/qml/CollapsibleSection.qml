import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: section

    property string title: ""
    property alias content: contentLoader.sourceComponent
    property bool expanded: false

    spacing: 0
    Layout.fillWidth: true

    // Section header
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 28
        color: sectionMouse.containsMouse ? "#3c3c3c" : "#333333"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 8

            Label {
                text: section.expanded ? "\u25BC" : "\u25B6"
                color: "#808080"
                font.pixelSize: 10
            }

            Label {
                text: section.title
                color: "#cccccc"
                font.pixelSize: 11
                font.bold: true
                Layout.fillWidth: true
            }
        }

        MouseArea {
            id: sectionMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: section.expanded = !section.expanded
        }
    }

    // Section content
    Loader {
        id: contentLoader
        Layout.fillWidth: true
        Layout.leftMargin: 15
        Layout.rightMargin: 10
        Layout.topMargin: 8
        Layout.bottomMargin: 8
        visible: section.expanded
    }
}
