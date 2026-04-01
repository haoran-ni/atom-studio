import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property color sourceColor: Qt.rgba(0, 0, 0, 1.0)
    property color defaultColor: Qt.rgba(0, 0, 0, 1.0)
    property bool showAlpha: false

    signal colorApplied(color newColor)

    Layout.fillWidth: true
    spacing: 6

    Rectangle {
        Layout.fillWidth: true
        height: 26
        radius: 8
        border.color: "#c7c9d1"
        border.width: 1
        color: Qt.rgba(
            redControl.currentValue / 255.0,
            greenControl.currentValue / 255.0,
            blueControl.currentValue / 255.0,
            root.showAlpha ? alphaControl.currentValue / 255.0 : 1.0
        )
    }

    NumericSliderControl {
        id: redControl
        title: qsTr("R")
        titleColor: "#b04848"
        titlePixelSize: 11
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
                root.showAlpha ? alphaControl.currentValue / 255.0 : 1.0
            ))
        }
    }

    NumericSliderControl {
        id: greenControl
        title: qsTr("G")
        titleColor: "#3d8f5a"
        titlePixelSize: 11
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
                root.showAlpha ? alphaControl.currentValue / 255.0 : 1.0
            ))
        }
    }

    NumericSliderControl {
        id: blueControl
        title: qsTr("B")
        titleColor: "#4068aa"
        titlePixelSize: 11
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
                root.showAlpha ? alphaControl.currentValue / 255.0 : 1.0
            ))
        }
    }

    NumericSliderControl {
        id: alphaControl
        visible: root.showAlpha
        title: qsTr("A")
        titleColor: "#7a7d87"
        titlePixelSize: 11
        integer: true
        from: 0
        to: 255
        stepSize: 1
        defaultValue: Math.round(root.defaultColor.a * 255)
        sourceValue: Math.round(root.sourceColor.a * 255)
        onValueApplied: function(newValue) {
            root.colorApplied(Qt.rgba(
                redControl.currentValue / 255.0,
                greenControl.currentValue / 255.0,
                blueControl.currentValue / 255.0,
                newValue / 255.0
            ))
        }
    }
}
