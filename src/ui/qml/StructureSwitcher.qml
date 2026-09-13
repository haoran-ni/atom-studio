import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: switcher
    objectName: "structureSwitcher"
    property int structureCount: 0
    property int activeIndex: -1
    property string structureLabel: ""
    signal activated(int index)

    implicitWidth: 400
    implicitHeight: 76
    radius: 12
    color: "#f5ffffff"
    border.color: "#c7c9d1"
    border.width: 1

    // Consume input on the floating surface so gestures never orbit/zoom the scene.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: function(wheel) { wheel.accepted = true }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 2
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: switcher.structureLabel
                color: "#17181c"
                font.pixelSize: 12
                font.weight: Font.Medium
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Label {
                text: (switcher.activeIndex + 1) + " / " + switcher.structureCount
                color: "#6e727d"
                font.pixelSize: 11
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ToolButton {
                objectName: "previousStructure"
                icon.source: "qrc:/icons/chevron-left.svg"
                icon.width: 12
                icon.height: 12
                implicitWidth: 28
                implicitHeight: 28
                enabled: switcher.activeIndex > 0
                Accessible.name: qsTr("Previous structure")
                onClicked: switcher.activated(switcher.activeIndex - 1)
            }
            Slider {
                id: slider
                objectName: "structureSlider"
                implicitHeight: 28
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, switcher.structureCount - 1)
                stepSize: 1
                snapMode: Slider.SnapAlways
                value: Math.max(0, switcher.activeIndex)
                Accessible.name: qsTr("Active structure")
                onMoved: switcher.activated(Math.round(value))
                background: Rectangle {
                    x: slider.leftPadding
                    y: slider.topPadding + slider.availableHeight / 2 - height / 2
                    width: slider.availableWidth
                    height: 4
                    radius: 2
                    color: "#d8dae0"
                    Rectangle {
                        width: slider.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: "#6e727d"
                    }
                }
                handle: Rectangle {
                    x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                    y: slider.topPadding + slider.availableHeight / 2 - height / 2
                    width: 16
                    height: 16
                    radius: 8
                    color: slider.pressed ? "#33353c" : "#ffffff"
                    border.color: slider.activeFocus ? "#17181c" : "#6e727d"
                    border.width: 2
                }
            }
            ToolButton {
                objectName: "nextStructure"
                icon.source: "qrc:/icons/chevron-right.svg"
                icon.width: 12
                icon.height: 12
                implicitWidth: 28
                implicitHeight: 28
                enabled: switcher.activeIndex < switcher.structureCount - 1
                Accessible.name: qsTr("Next structure")
                onClicked: switcher.activated(switcher.activeIndex + 1)
            }
        }
    }
}
