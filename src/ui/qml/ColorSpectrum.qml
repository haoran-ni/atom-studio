import QtQuick
import "ColorUtils.js" as ColorUtils

Item {
    id: root

    property real hue: 0
    property real saturation: 1
    property real lightness: 0.5
    property color selectedColor: "red"

    signal colorEdited(real hue, real lightness)
    signal editFinished()

    activeFocusOnTab: true
    Accessible.role: Accessible.Slider
    Accessible.name: qsTr("Color spectrum")
    Accessible.description: qsTr("Left and right adjust lightness. Up and down adjust hue.")

    function choose(x, y) {
        colorEdited(ColorUtils.clamp(y / height, 0, 1),
                    1 - ColorUtils.clamp(x / width, 0, 1))
    }

    Keys.onPressed: function(event) {
        const step = event.modifiers & Qt.ShiftModifier ? 0.05 : 0.005
        if (event.key === Qt.Key_Left)
            colorEdited(hue, ColorUtils.clamp(lightness + step, 0, 1))
        else if (event.key === Qt.Key_Right)
            colorEdited(hue, ColorUtils.clamp(lightness - step, 0, 1))
        else if (event.key === Qt.Key_Up)
            colorEdited(ColorUtils.clamp(hue - step, 0, 1), lightness)
        else if (event.key === Qt.Key_Down)
            colorEdited(ColorUtils.clamp(hue + step, 0, 1), lightness)
        else
            return
        event.accepted = true
        editFinished()
    }

    Rectangle {
        anchors.fill: parent
        radius: 6
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.hsla(0, root.saturation, 0.5, 1) }
            GradientStop { position: 1/6; color: Qt.hsla(1/6, root.saturation, 0.5, 1) }
            GradientStop { position: 2/6; color: Qt.hsla(2/6, root.saturation, 0.5, 1) }
            GradientStop { position: 3/6; color: Qt.hsla(3/6, root.saturation, 0.5, 1) }
            GradientStop { position: 4/6; color: Qt.hsla(4/6, root.saturation, 0.5, 1) }
            GradientStop { position: 5/6; color: Qt.hsla(5/6, root.saturation, 0.5, 1) }
            GradientStop { position: 1; color: Qt.hsla(1, root.saturation, 0.5, 1) }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 6
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "white" }
            GradientStop { position: 0.5; color: "#00ffffff" }
            GradientStop { position: 0.5001; color: "#00000000" }
            GradientStop { position: 1; color: "black" }
        }
        border.width: root.activeFocus ? 2 : 0
        border.color: "#246bdb"
    }

    Rectangle {
        x: (1 - root.lightness) * root.width - width / 2
        y: root.hue * root.height - height / 2
        width: 20
        height: 20
        radius: 10
        color: root.selectedColor
        border.color: "white"
        border.width: 3

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: width / 2
            color: "transparent"
            border.color: "#6c6c70"
        }
    }

    MouseArea {
        anchors.fill: parent
        preventStealing: true
        onPressed: function(mouse) {
            root.forceActiveFocus()
            root.choose(mouse.x, mouse.y)
        }
        onPositionChanged: function(mouse) {
            if (pressed)
                root.choose(mouse.x, mouse.y)
        }
        onReleased: root.editFinished()
        onCanceled: root.editFinished()
    }
}
