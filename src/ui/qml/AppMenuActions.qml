import QtQuick
import QtQuick.Controls
import AtomStudio 1.0

QtObject {
    // File actions
    property Action fileOpen: Action {
        text: qsTr("Open...")
        shortcut: StandardKey.Open
        onTriggered: FileController.openFileDialog()
    }

    property Action fileOpenRecent: Action {
        text: qsTr("Open Recent")
        enabled: false
    }

    property Action fileSaveImage: Action {
        text: qsTr("Save Image...")
        shortcut: StandardKey.Save
        onTriggered: console.log("Save image triggered")
    }

    property Action fileExport: Action {
        text: qsTr("Export...")
        onTriggered: console.log("Export triggered")
    }

    property Action fileQuit: Action {
        text: qsTr("Quit")
        shortcut: StandardKey.Quit
        onTriggered: Qt.quit()
    }

    // Edit actions
    property Action editUndo: Action {
        text: qsTr("Undo")
        shortcut: StandardKey.Undo
        enabled: false
    }

    property Action editRedo: Action {
        text: qsTr("Redo")
        shortcut: StandardKey.Redo
        enabled: false
    }

    property Action editSelectAll: Action {
        text: qsTr("Select All")
        shortcut: StandardKey.SelectAll
        onTriggered: console.log("Select all triggered")
    }

    property Action editDeselectAll: Action {
        text: qsTr("Deselect All")
        onTriggered: console.log("Deselect all triggered")
    }

    property Action editPreferences: Action {
        text: qsTr("Preferences...")
        onTriggered: console.log("Preferences triggered")
    }
}
