import QtQuick
import QtQuick.Layouts

Rectangle {
    default property alias items: contentColumn.data

    color: "#000000"
    opacity: 0.5
    radius: 4
    width: contentColumn.implicitWidth + 16
    height: contentColumn.implicitHeight + 12

    ColumnLayout {
        id: contentColumn
        anchors.centerIn: parent
        spacing: 2
    }
}
