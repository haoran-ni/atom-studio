import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AtomStudio 1.0

ColumnLayout {
    id: root
    required property ColorPickerPopup pickerPopup
    readonly property bool canEdit: !StructureModel.switchingLocked && !StructureModel.editsLocked
    readonly property real columnSpacing: 8
    readonly property real idWidth: Math.max(22, Math.ceil(rowMetrics.advanceWidth(StructureModel.atomProperties.largestDisplayId)))
    readonly property real speciesWidth: {
        const labels = StructureModel.atomProperties.speciesLabels
        let width = rowMetrics.advanceWidth(qsTr("Species"))
        for (let i = 0; i < labels.length; ++i)
            width = Math.max(width, rowMetrics.advanceWidth(labels[i]))
        return Math.ceil(width)
    }
    readonly property real columnsWidth: 16 + idWidth + 32 + speciesWidth + 88 + 3 * columnSpacing
    signal discardDrafts()
    spacing: 5

    function closePicker() {
        if (pickerPopup.owner && pickerPopup.owner.speciesEditor === true)
            pickerPopup.release(pickerPopup.owner)
    }
    onVisibleChanged: { if (!visible) closePicker() }
    onCanEditChanged: { if (!canEdit) closePicker() }
    Component.onDestruction: closePicker()

    Connections {
        target: StructureModel.atomProperties
        function onModelAboutToBeReset() { root.closePicker(); root.discardDrafts() }
    }

    FontMetrics { id: rowMetrics; font.pixelSize: 13 }

    Label {
        objectName: "perAtomPropertiesTitle"
        Layout.fillWidth: true
        text: qsTr("Per-atom properties")
        color: "#17181c"
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }

    Rectangle {
        id: panel
        objectName: "atomPropertiesPanel"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(248, 30 + atoms.count * 36 + 14)
        color: "#ffffff"
        radius: 6
        clip: true

        Item {
            id: header
            x: 1; y: 1
            width: parent.width - 2
            height: 28
            clip: true
            Rectangle {
                anchors.fill: parent
                color: "#f3f4f7"
                radius: panel.radius - 1
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: parent.radius
                    color: parent.color
                }
            }
            Row {
                x: 8 - atoms.contentX
                spacing: root.columnSpacing
                Label { width: root.idWidth; text: qsTr("ID"); font.pixelSize: 12; color: "#555861" }
                Label { width: 32; text: qsTr("Color"); font.pixelSize: 12; color: "#555861" }
                Label { width: root.speciesWidth; text: qsTr("Species"); font.pixelSize: 12; color: "#555861" }
                Label { width: 88; text: qsTr("Radius (Å)"); font.pixelSize: 12; color: "#555861" }
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        ListView {
            id: atoms
            objectName: "atomPropertiesList"
            anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; margins: 1; bottomMargin: 13 }
            clip: true
            model: StructureModel.atomProperties
            reuseItems: true
            contentWidth: Math.max(width, root.columnsWidth)
            flickableDirection: Flickable.AutoFlickDirection
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                objectName: "atomPropertiesVerticalScrollBar"
                policy: ScrollBar.AsNeeded
                active: true
                visible: atoms.contentHeight > atoms.height + 1
            }
            ScrollBar.horizontal: ScrollBar {
                objectName: "atomPropertiesHorizontalScrollBar"
                parent: panel
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 2 }
                policy: ScrollBar.AsNeeded
                active: true
                visible: atoms.contentWidth > atoms.width + 1
            }
            onContentXChanged: root.closePicker()
            onContentYChanged: root.closePicker()

            delegate: Rectangle {
                id: atomRow
                required property int index
                required property var atomIdentifier
                required property int atomicNumber
                required property string speciesName
                required property color atomColor
                required property real atomRadius
                width: atoms.contentWidth
                height: 36
                color: index % 2 === 0 ? "#ffffff" : "#f8f8fa"
                ListView.onPooled: root.pickerPopup.release(colorButton)
                ListView.onReused: radiusInput.restore()
                Component.onDestruction: root.pickerPopup.release(colorButton)
                onAtomRadiusChanged: radiusInput.restore()
                onAtomColorChanged: root.pickerPopup.syncFrom(colorButton)
                Connections {
                    target: root
                    function onDiscardDrafts() { radiusInput.restore() }
                }

                Row {
                    x: 8
                    height: parent.height
                    spacing: root.columnSpacing
                    Label {
                        objectName: "atomIdentifier" + atomRow.atomIdentifier
                        width: root.idWidth; height: parent.height
                        text: atomRow.atomIdentifier
                        color: "#33353c"; font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        ToolTip.visible: idHover.hovered
                        ToolTip.text: text
                        HoverHandler { id: idHover }
                    }
                    Button {
                        id: colorButton
                        objectName: "speciesColor" + atomRow.atomIdentifier
                        width: 32; height: 28
                        anchors.verticalCenter: parent.verticalCenter
                        enabled: root.canEdit
                        property bool speciesEditor: true
                        property bool selectionDependent: false
                        property bool showAlpha: false
                        property string title: atomRow.speciesName
                        property color sourceColor: atomRow.atomColor
                        property color defaultColor: sourceColor
                        readonly property Item anchorItem: colorButton
                        signal colorApplied(color value)
                        onColorApplied: function(value) { StructureModel.applySpeciesColor(atomRow.atomicNumber, value) }
                        Accessible.name: qsTr("Color for %1, atom %2").arg(atomRow.speciesName).arg(atomRow.atomIdentifier)
                        onClicked: {
                            defaultColor = StructureModel.speciesDefaultColor(atomRow.atomicNumber)
                            root.pickerPopup.toggleFor(colorButton)
                        }
                        background: Rectangle {
                            anchors.centerIn: parent
                            width: 24; height: 24; radius: 2
                            color: atomRow.atomColor
                            border.color: colorButton.activeFocus ? "#246bdb" : "#92959d"
                            border.width: colorButton.activeFocus ? 2 : 1
                        }
                    }
                    Label {
                        width: root.speciesWidth; height: parent.height
                        text: atomRow.speciesName
                        color: "#33353c"; font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                    }
                    TextField {
                        id: radiusInput
                        objectName: "speciesRadius" + atomRow.atomIdentifier
                        width: 88; height: 28
                        anchors.verticalCenter: parent.verticalCenter
                        enabled: root.canEdit
                        text: Number(atomRow.atomRadius.toPrecision(7)).toString()
                        property bool invalid: false
                        property var editingDocument: -1
                        color: "#17181c"; font.pixelSize: 13
                        leftPadding: 6; rightPadding: 6
                        selectByMouse: true
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        Accessible.name: qsTr("Radius in angstroms for %1, atom %2").arg(atomRow.speciesName).arg(atomRow.atomIdentifier)
                        function restore() {
                            text = Number(atomRow.atomRadius.toPrecision(7)).toString()
                            invalid = false
                        }
                        function commit() {
                            if (editingDocument !== StructureModel.activeId || !root.canEdit) { restore(); return }
                            if (!StructureModel.applySpeciesRadius(atomRow.atomicNumber, Number(text))) {
                                invalid = true
                                return
                            }
                            restore()
                        }
                        Keys.onReturnPressed: commit()
                        Keys.onEnterPressed: commit()
                        Keys.onEscapePressed: { restore(); focus = false }
                        onEditingFinished: { if (text !== Number(atomRow.atomRadius.toPrecision(7)).toString()) commit() }
                        onTextEdited: invalid = false
                        onActiveFocusChanged: { if (activeFocus) editingDocument = StructureModel.activeId }
                        background: Rectangle {
                            radius: 4
                            color: radiusInput.activeFocus ? "#f3f4f7" : "transparent"
                            border.color: radiusInput.invalid ? "#b42318" : radiusInput.activeFocus ? "#92959d" : "transparent"
                        }
                        ToolTip.visible: invalid
                        ToolTip.text: qsTr("Enter a finite radius greater than zero (Å).")
                    }
                }
            }
        }

        // Draw the complete rounded outline above the header and row backgrounds.
        Rectangle {
            anchors.fill: parent
            z: 1
            color: "transparent"
            radius: panel.radius
            border.width: 1
            border.color: "#c7c9d1"
        }
    }
}
