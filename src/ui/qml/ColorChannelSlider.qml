import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property string title: ""
    property int value: 0
    property int maximum: 255
    property string suffix: ""
    property color startColor: "black"
    property color endColor: "white"
    readonly property bool editing: numberField.activeFocus

    signal valueEdited(int newValue)
    signal editFinished()

    spacing: 5
    Layout.fillWidth: true

    function commitText() {
        const raw = numberField.text.trim().replace(/%$/, "").trim()
        const valid = /^\d+$/.test(raw) && Number(raw) <= maximum
        if (valid)
            valueEdited(Number(raw))
        numberField.text = value + suffix
        numberField.invalid = !valid
        editFinished()
    }

    Label {
        text: root.title
        font.pixelSize: 11
        font.weight: Font.Medium
        color: "#777980"
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        Slider {
            id: slider
            objectName: root.objectName + "Slider"
            Layout.fillWidth: true
            implicitHeight: 30
            leftPadding: 0
            rightPadding: 0
            from: 0
            to: root.maximum
            stepSize: 1
            value: root.value
            Accessible.name: root.title
            onMoved: root.valueEdited(Math.round(value))
            onPressedChanged: {
                if (!pressed)
                    root.editFinished()
            }

            background: Item {
                x: slider.leftPadding
                y: (slider.height - height) / 2
                width: slider.availableWidth
                height: 28

                ColorSwatch {
                    anchors.fill: parent
                    color: "transparent"
                    radius: 14
                    borderWidth: 0
                }

                Rectangle {
                    anchors.fill: parent
                    radius: 14
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: root.startColor }
                        GradientStop { position: 1; color: root.endColor }
                    }
                }
            }

            handle: Rectangle {
                x: slider.visualPosition * (slider.availableWidth - width)
                y: (slider.height - height) / 2
                width: 28
                height: 28
                radius: 14
                color: "transparent"
                border.width: 3
                border.color: "white"

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -1
                    radius: width / 2
                    color: "transparent"
                    border.width: slider.activeFocus ? 2 : 1
                    border.color: slider.activeFocus ? "#246bdb" : "#6c6c70"
                }
            }
        }

        TextField {
            id: numberField
            objectName: root.objectName + "Field"
            property bool invalid: false
            Layout.preferredWidth: 62
            implicitHeight: 30
            text: root.value + root.suffix
            horizontalAlignment: TextInput.AlignHCenter
            selectByMouse: true
            font.pixelSize: 13
            color: "#17181c"
            inputMethodHints: Qt.ImhDigitsOnly
            Accessible.name: root.title + qsTr(" value")
            onTextEdited: invalid = false
            onEditingFinished: {
                if (text !== root.value + root.suffix)
                    root.commitText()
            }
            onAccepted: focus = false
            background: Rectangle {
                radius: 6
                color: "#ffffff"
                border.width: 1
                border.color: numberField.invalid ? "#cf4242"
                              : numberField.activeFocus ? "#246bdb" : "#e5e5e8"
            }
            ToolTip.visible: invalid && hovered
            ToolTip.text: qsTr("Enter a whole number from 0 to %1.").arg(root.maximum)
        }
    }

    onValueChanged: {
        if (!numberField.activeFocus)
            numberField.text = value + suffix
    }
}
