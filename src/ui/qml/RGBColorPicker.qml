import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property color sourceColor: Qt.rgba(0, 0, 0, 1.0)
    property color defaultColor: Qt.rgba(0, 0, 0, 1.0)

    signal colorApplied(color newColor)

    Layout.fillWidth: true
    spacing: 8

    Label {
        text: qsTr("RGB Color")
        color: "#cccccc"
        font.pixelSize: 11
    }

    Rectangle {
        Layout.fillWidth: true
        height: 24
        radius: 2
        border.color: "#3c3c3c"
        border.width: 1
        color: Qt.rgba(
            redControl.currentValue / 255.0,
            greenControl.currentValue / 255.0,
            blueControl.currentValue / 255.0,
            1.0
        )
    }

    NumericSliderControl {
        id: redControl
        title: qsTr("R")
        titleColor: "#ff7777"
        titlePixelSize: 10
        integer: true
        from: 0
        to: 255
        stepSize: 1
        defaultValue: Math.round(root.defaultColor.r * 255)
        sourceValue: Math.round(root.sourceColor.r * 255)
        onValueApplied: function(newValue) {
            root.colorApplied(Qt.rgba(
                newValue / 255.0,
                greenControl.currentValue / 255.0,
                blueControl.currentValue / 255.0,
                1.0
            ))
        }
    }

    NumericSliderControl {
        id: greenControl
        title: qsTr("G")
        titleColor: "#77ff77"
        titlePixelSize: 10
        integer: true
        from: 0
        to: 255
        stepSize: 1
        defaultValue: Math.round(root.defaultColor.g * 255)
        sourceValue: Math.round(root.sourceColor.g * 255)
        onValueApplied: function(newValue) {
            root.colorApplied(Qt.rgba(
                redControl.currentValue / 255.0,
                newValue / 255.0,
                blueControl.currentValue / 255.0,
                1.0
            ))
        }
    }

    NumericSliderControl {
        id: blueControl
        title: qsTr("B")
        titleColor: "#7777ff"
        titlePixelSize: 10
        integer: true
        from: 0
        to: 255
        stepSize: 1
        defaultValue: Math.round(root.defaultColor.b * 255)
        sourceValue: Math.round(root.sourceColor.b * 255)
        onValueApplied: function(newValue) {
            root.colorApplied(Qt.rgba(
                redControl.currentValue / 255.0,
                greenControl.currentValue / 255.0,
                newValue / 255.0,
                1.0
            ))
        }
    }
}
