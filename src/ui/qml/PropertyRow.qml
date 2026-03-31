import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    property string label: ""
    property string value: ""

    spacing: 10
    Layout.fillWidth: true

    Label {
        text: label
        color: "#808080"
        font.pixelSize: 11
        Layout.preferredWidth: 80
    }

    Label {
        text: value
        color: "#cccccc"
        font.pixelSize: 11
        Layout.fillWidth: true
        elide: Text.ElideRight
    }
}
