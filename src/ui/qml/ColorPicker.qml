import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root

    property string title: qsTr("Color")
    property color sourceColor: "black"
    property color defaultColor: "black"
    property bool showAlpha: false
    property bool selectionDependent: false
    required property ColorPickerPopup pickerPopup
    readonly property alias anchorItem: colorButton

    signal colorApplied(color newColor)

    Layout.fillWidth: true
    spacing: 10
    opacity: enabled ? 1 : 0.4

    onSourceColorChanged: pickerPopup.syncFrom(root)
    onWidthChanged: {
        if (pickerPopup && pickerPopup.owner === root)
            pickerPopup.reposition()
    }
    onEnabledChanged: {
        if (!enabled)
            pickerPopup.release(root)
    }
    onVisibleChanged: {
        if (!visible)
            pickerPopup.release(root)
    }
    Component.onDestruction: pickerPopup.release(root)

    Label {
        text: root.title
        Layout.fillWidth: true
        color: "#17181c"
        font.pixelSize: 13
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    Button {
        id: colorButton
        objectName: "colorPickerButton"
        implicitWidth: 36
        implicitHeight: 36
        padding: 0
        Accessible.name: root.title
        Accessible.description: qsTr("Open color picker")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Choose %1").arg(root.title.toLowerCase())
        onClicked: root.pickerPopup.toggleFor(root)

        background: Rectangle {
            radius: width / 2
            color: "transparent"
            border.color: colorButton.activeFocus ? "#246bdb" : "transparent"
            border.width: 2
        }

        contentItem: Item {
            Image {
                anchors.fill: parent
                anchors.margins: 2
                source: "qrc:/icons/color-ring.svg"
                sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                opacity: colorButton.down ? 0.75 : 1
            }

            ColorSwatch {
                anchors.fill: parent
                anchors.margins: 8
                radius: width / 2
                color: root.sourceColor
                borderWidth: 0
            }
        }
    }
}
