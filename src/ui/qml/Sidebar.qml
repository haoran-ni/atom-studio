import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AtomStudio 1.0

Rectangle {
    id: sidebar
    signal openInteractiveShell()
    signal openShellTutorial()
    signal openPythonPackages()
    signal openEnvironmentTerminal()

    property var viewport: null
    property string maxRTSamplesErrorMessage: ""
    property string statusHint: ""
    property var statusHintOwner: null
    readonly property bool rtSettingsVisible: sidebar.viewport && sidebar.viewport.rendererMode === 1
    readonly property color defaultBackgroundColor: "white"

    readonly property color panelBgTop: "#fbfbfc"
    readonly property color panelBgBottom: "#efeff2"
    readonly property color tabFill: "#d8dae0"
    readonly property color buttonFill: "#e2e3e8"
    readonly property color inputFill: "#ffffff"
    readonly property color inputFillAlt: "#f3f4f7"
    readonly property color borderSoft: "#c7c9d1"
    readonly property color textStrong: "#17181c"
    readonly property color textBody: "#33353c"
    readonly property color textMuted: "#7a7d87"
    readonly property color connectorColor: "#6e727d"

    ColorPickerPopup {
        id: sharedColorPicker
    }

    Connections {
        target: StructureModel
        function onSelectionChanged() {
            if (sharedColorPicker.owner && sharedColorPicker.owner.selectionDependent)
                sharedColorPicker.release(sharedColorPicker.owner)
        }
    }

    Connections {
        target: sidebarScrollView.contentItem
        function onContentYChanged() {
            if (sharedColorPicker.visible)
                sharedColorPicker.close()
        }
    }

    function showMaxRTSamplesError(message) {
        maxRTSamplesErrorMessage = message
        maxRTSamplesErrorDialog.title = qsTr("Invalid Sample Limit")
        maxRTSamplesErrorDialog.open()
    }

    function showSliderInputError(message) {
        maxRTSamplesErrorMessage = message
        maxRTSamplesErrorDialog.title = qsTr("Invalid Value")
        maxRTSamplesErrorDialog.open()
    }

    function setStatusHint(owner, message) {
        statusHintOwner = owner
        statusHint = message
    }

    function clearStatusHint(owner) {
        if (statusHintOwner === owner) {
            statusHintOwner = null
            statusHint = ""
        }
    }

    function collapseOtherSections(activeSection) {
        for (var i = 0; i < sectionsLayout.children.length; ++i) {
            var child = sectionsLayout.children[i]
            if (child !== activeSection && child.expanded !== undefined) {
                child.expanded = false
            }
        }
    }

    function resetRayTracingSettings() {
        if (!sidebar.viewport) {
            return
        }

        sidebar.viewport.maxRTSamples = 1000
        sidebar.viewport.enableAO = false
        sidebar.viewport.enableShadows = false
        sidebar.viewport.aoSamples = 4
        sidebar.viewport.aoRadius = 3.0
        sidebar.viewport.ambientStrength = 0.50
        sidebar.viewport.diffuseStrength = 0.7
        sidebar.viewport.specularStrength = 0.05
        sidebar.viewport.shininess = 60
        sidebar.viewport.lightAzimuth = 0
        sidebar.viewport.lightElevation = 45
    }

    color: "transparent"

    gradient: Gradient {
        GradientStop { position: 0.0; color: panelBgTop }
        GradientStop { position: 1.0; color: panelBgBottom }
    }

    component SidebarButton: Button {
        id: control

        implicitHeight: 38

        contentItem: Text {
            text: control.text
            color: control.enabled ? sidebar.textStrong : sidebar.textMuted
            font.pixelSize: 13
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: 12
            color: control.down ? Qt.darker(sidebar.buttonFill, 1.08) : sidebar.buttonFill
            opacity: control.enabled ? 1.0 : 0.55
        }
    }

    component SidebarTextField: TextField {
        id: control

        implicitHeight: 36
        color: sidebar.textStrong
        selectedTextColor: "#101522"
        selectionColor: "#d8deed"
        placeholderTextColor: sidebar.textMuted
        font.pixelSize: 13
        leftPadding: 12
        rightPadding: 12
        topPadding: 8
        bottomPadding: 8

        background: Rectangle {
            radius: 10
            color: control.activeFocus ? sidebar.inputFillAlt : sidebar.inputFill
            border.width: control.activeFocus ? 1 : 0
            border.color: sidebar.borderSoft
        }
    }

    component ReplicationArrow: Button {
        id: arrow

        property string iconSource: ""
        implicitWidth: 18
        implicitHeight: 18
        padding: 2
        focusPolicy: Qt.NoFocus

        contentItem: Image {
            source: arrow.iconSource
            sourceSize.width: 14
            sourceSize.height: 14
            fillMode: Image.PreserveAspectFit
            opacity: arrow.enabled ? 1.0 : 0.3
        }
        background: Rectangle {
            radius: 3
            color: arrow.down ? sidebar.buttonFill
                              : arrow.hovered ? sidebar.inputFillAlt : "transparent"
        }
    }

    component ReplicationControl: RowLayout {
        id: control

        property int value: 1
        property string axisName: ""
        signal valueCommitted(int newValue)
        spacing: 2

        function commitText() {
            if (!input.acceptableInput) {
                sidebar.showSliderInputError(qsTr("Replication factors must be integers from 1 to 99."))
                input.text = control.value.toString()
                return
            }
            const requested = Number(input.text)
            if (requested !== control.value)
                control.valueCommitted(requested)
            input.text = control.value.toString()
        }

        function step(delta) {
            const requested = Math.max(1, Math.min(99, control.value + delta))
            if (requested !== control.value)
                control.valueCommitted(requested)
            input.text = control.value.toString()
        }

        SidebarTextField {
            id: input
            objectName: "replication" + control.axisName + "Input"
            Layout.fillWidth: true
            Layout.minimumWidth: 26
            leftPadding: 2
            rightPadding: 2
            text: control.value.toString()
            horizontalAlignment: TextInput.AlignHCenter
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 1; top: 99 }
            selectByMouse: true
            Accessible.name: qsTr("Replication along %1").arg(control.axisName)
            // Enter commits; leaving the field discards an uncommitted draft.
            Keys.onReturnPressed: control.commitText()
            Keys.onEnterPressed: control.commitText()
            onActiveFocusChanged: {
                if (!activeFocus)
                    text = control.value.toString()
            }
        }

        // Explicitly refresh after a model update even if text was assigned
        // while editing. The applied value remains owned by StructureModel.
        onValueChanged: input.text = control.value.toString()

        ColumnLayout {
            spacing: 0
            ReplicationArrow {
                objectName: "replication" + control.axisName + "Up"
                iconSource: "qrc:/icons/chevron-up.svg"
                enabled: control.value < 99
                Accessible.name: qsTr("Increase %1 replication").arg(control.axisName)
                onClicked: control.step(1)
            }
            ReplicationArrow {
                objectName: "replication" + control.axisName + "Down"
                iconSource: "qrc:/icons/chevron-down.svg"
                enabled: control.value > 1
                Accessible.name: qsTr("Decrease %1 replication").arg(control.axisName)
                onClicked: control.step(-1)
            }
        }
    }

    component SidebarCheckBox: CheckBox {
        id: control

        spacing: 10

        indicator: Rectangle {
            implicitWidth: 17
            implicitHeight: 17
            anchors.verticalCenter: parent.verticalCenter
            radius: 5
            color: control.checked ? "#e4e9f6" : sidebar.inputFill
            border.width: control.checked ? 0 : 1
            border.color: sidebar.borderSoft

            Rectangle {
                anchors.centerIn: parent
                width: 7
                height: 7
                radius: 3.5
                visible: control.checked
                color: "#25283a"
            }
        }

        contentItem: Text {
            text: control.text
            color: control.enabled ? sidebar.textBody : sidebar.textMuted
            font.pixelSize: 13
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
            leftPadding: control.indicator.width + control.spacing
        }
    }

    component SidebarComboBox: ComboBox {
        id: control

        implicitHeight: 38
        leftPadding: 12
        rightPadding: 34

        contentItem: Text {
            text: control.displayText
            color: control.enabled ? sidebar.textStrong : sidebar.textMuted
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        indicator: Image {
            source: control.popup.visible ? "qrc:/icons/chevron-down.svg" : "qrc:/icons/chevron-right.svg"
            width: 16
            height: 16
            sourceSize: Qt.size(16, 16)
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 12
        }

        background: Rectangle {
            radius: 12
            color: sidebar.buttonFill
        }

        delegate: ItemDelegate {
            required property int index
            required property var modelData

            width: ListView.view ? ListView.view.width : control.width
            implicitHeight: 34
            topPadding: 0
            bottomPadding: 0
            leftPadding: 0
            rightPadding: 0
            highlighted: control.highlightedIndex === index

            contentItem: Text {
                text: modelData
                color: highlighted ? sidebar.textStrong : sidebar.textBody
                font.pixelSize: 13
                leftPadding: 12
                rightPadding: 12
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                radius: 8
                color: highlighted ? sidebar.buttonFill : "transparent"
            }
        }

        popup: Popup {
            y: control.height + 4
            width: control.width
            padding: 6

            background: Rectangle {
                radius: 12
                color: sidebar.inputFill
                border.width: 1
                border.color: sidebar.borderSoft
            }

            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: control.delegateModel
                currentIndex: control.highlightedIndex
                spacing: 2
            }
        }
    }

    component SidebarPropertyRow: RowLayout {
        property string label: ""
        property string value: ""

        spacing: 10
        Layout.fillWidth: true

        Label {
            visible: parent.label.length > 0
            text: parent.label
            color: sidebar.textMuted
            font.pixelSize: 13
            Layout.preferredWidth: parent.label.length > 0 ? 90 : 0
            Layout.alignment: Qt.AlignTop
        }

        Label {
            text: parent.value
            color: sidebar.textBody
            font.pixelSize: 13
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            wrapMode: Text.WrapAnywhere
        }
    }

    component SidebarBranchRow: Item {
        property bool lastItem: false
        property alias content: contentLoader.sourceComponent

        Layout.fillWidth: true
        implicitHeight: contentLoader.implicitHeight

        Rectangle {
            x: 10
            y: 0
            width: 2
            height: contentLoader.implicitHeight
            radius: 1
            color: sidebar.connectorColor
            opacity: 0.45
        }

        Loader {
            id: contentLoader
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.right: parent.right
            anchors.top: parent.top
        }
    }

    component SidebarSection: ColumnLayout {
        id: section

        property string title: ""
        property string iconSource: ""
        property alias content: contentLoader.sourceComponent
        property bool expanded: false
        property real expansion: expanded ? 1.0 : 0.0

        spacing: 8
        Layout.fillWidth: true

        Behavior on expansion {
            NumberAnimation {
                duration: 180
                easing.type: Easing.OutCubic
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 40

            Rectangle {
                anchors.fill: parent
                radius: 14
                color: sidebar.tabFill
                opacity: section.expanded ? 1.0 : 0.0
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10

                Image {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    source: section.iconSource
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }

                Label {
                    text: section.title
                    color: section.expanded ? sidebar.textStrong : sidebar.textMuted
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }

                Image {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    source: "qrc:/icons/chevron-right.svg"
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                    rotation: section.expansion * 90

                    Behavior on rotation {
                        NumberAnimation {
                            duration: 180
                            easing.type: Easing.OutCubic
                        }
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (section.expanded) {
                        section.expanded = false
                    } else {
                        sidebar.collapseOtherSections(section)
                        section.expanded = true
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: contentLoader.implicitHeight * section.expansion
            opacity: section.expansion
            clip: true
            visible: section.expansion > 0.001 || contentLoader.active

            Loader {
                id: contentLoader
                anchors.left: parent.left
                anchors.right: parent.right
                active: section.expanded || section.expansion > 0.001
            }

            Behavior on opacity {
                NumberAnimation {
                    duration: 120
                    easing.type: Easing.OutCubic
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        ScrollView {
            id: sidebarScrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            background: Item {}

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            ColumnLayout {
                id: sectionsLayout
                width: Math.max(0, sidebar.width - 28)
                spacing: 10

                SidebarSection {
                    id: filesSection
                    title: qsTr("Files")
                    iconSource: "qrc:/icons/files.svg"
                    expanded: true
                    Layout.fillWidth: true

                    content: Component {
                        ColumnLayout {
                            spacing: 8

                            SidebarBranchRow {
                                content: Component {
                                    SidebarButton {
                                        text: qsTr("Import Structures")
                                        Layout.fillWidth: true
                                        onClicked: FileController.openFileDialog()
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    SidebarComboBox {
                                        Layout.fillWidth: true
                                        displayText: qsTr("Export Structures")
                                        model: FileController.structureExportFormats
                                        enabled: StructureModel.hasStructure
                                                 && !FileController.isExporting
                                        onActivated: FileController.openSaveStructureDialog(currentText)
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    ColumnLayout {
                                        spacing: 6

                                        SidebarComboBox {
                                            id: exportImagesComboBox
                                            Layout.fillWidth: true
                                            displayText: qsTr("Export Images")
                                            model: {
                                                var a = sidebar.viewport ? sidebar.viewport.backgroundColor.a : 1.0
                                                return a < 0.99 ? [".png"] : [".png", ".jpg", ".pdf"]
                                            }
                                            enabled: StructureModel.hasStructure
                                            onActivated: FileController.openSaveImageDialog(
                                                currentText, exportImagesIncludeAxes.checked,
                                                exportImagesTransparentBackground.checked)
                                        }

                                        SidebarCheckBox {
                                            id: exportImagesIncludeAxes
                                            text: qsTr("Include Axes")
                                            enabled: StructureModel.hasStructure
                                        }

                                        SidebarCheckBox {
                                            id: exportImagesTransparentBackground
                                            text: qsTr("Transparent Background")
                                            enabled: StructureModel.hasStructure
                                            ToolTip.visible: hovered
                                            ToolTip.text: qsTr("Use a transparent background when exporting PNG images.")
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                SidebarSection {
                    id: structureInfoSection
                    title: qsTr("Info")
                    iconSource: "qrc:/icons/info.svg"
                    expanded: false
                    Layout.fillWidth: true

                    content: Component {
                        ColumnLayout {
                            spacing: 8

                            SidebarBranchRow {
                                content: Component { SidebarPropertyRow { label: qsTr("File:"); value: StructureModel.fileName } }
                            }
                            SidebarBranchRow {
                                content: Component { SidebarPropertyRow { label: qsTr("Atoms:"); value: StructureModel.atomCount.toString() } }
                            }
                            SidebarBranchRow {
                                content: Component {
                                    SidebarPropertyRow {
                                        label: qsTr("Atom Types:")
                                        value: StructureModel.elements.join("\n")
                                    }
                                }
                            }
                            SidebarBranchRow {
                                visible: StructureModel.hasUnitCell
                                content: Component {
                                    SidebarPropertyRow {
                                        label: qsTr("Unit Cell:")
                                        value: StructureModel.cellParameters
                                    }
                                }
                            }
                        }
                    }
                }

                SidebarSection {
                    id: structureManipulationSection
                    objectName: "structureManipulationSection"
                    title: qsTr("Structure")
                    iconSource: "qrc:/icons/structure.svg"
                    Layout.fillWidth: true

                    content: Component {
                        ColumnLayout {
                            spacing: 8

                            SidebarBranchRow {
                                lastItem: true
                                content: Component {
                                    ColumnLayout {
                                        spacing: 6

                                        Label {
                                            text: qsTr("Replicate All Unit Cells")
                                            color: sidebar.textStrong
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                            implicitHeight: replicateControls.implicitHeight

                                            ColumnLayout {
                                                id: replicateControls
                                                width: parent.width
                                                spacing: 6
                                                enabled: StructureModel.hasReplicableStructures && !StructureModel.editsLocked
                                                opacity: enabled ? 1.0 : 0.4

                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 6

                                                    Label {
                                                        text: qsTr("X:")
                                                        color: sidebar.textBody
                                                        font.pixelSize: 12
                                                        Layout.preferredWidth: 16
                                                    }
                                                    ReplicationControl {
                                                        Layout.fillWidth: true
                                                        axisName: "X"
                                                        value: StructureModel.replicationX
                                                        onValueCommitted: function(newValue) {
                                                            StructureModel.replicateCell(newValue, StructureModel.replicationY, StructureModel.replicationZ)
                                                        }
                                                    }

                                                    Label {
                                                        text: qsTr("Y:")
                                                        color: sidebar.textBody
                                                        font.pixelSize: 12
                                                        Layout.preferredWidth: 16
                                                    }
                                                    ReplicationControl {
                                                        Layout.fillWidth: true
                                                        axisName: "Y"
                                                        value: StructureModel.replicationY
                                                        onValueCommitted: function(newValue) {
                                                            StructureModel.replicateCell(StructureModel.replicationX, newValue, StructureModel.replicationZ)
                                                        }
                                                    }

                                                    Label {
                                                        text: qsTr("Z:")
                                                        color: sidebar.textBody
                                                        font.pixelSize: 12
                                                        Layout.preferredWidth: 16
                                                    }
                                                    ReplicationControl {
                                                        Layout.fillWidth: true
                                                        axisName: "Z"
                                                        value: StructureModel.replicationZ
                                                        onValueCommitted: function(newValue) {
                                                            StructureModel.replicateCell(StructureModel.replicationX, StructureModel.replicationY, newValue)
                                                        }
                                                    }
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                enabled: !StructureModel.hasReplicableStructures
                                                onContainsMouseChanged: {
                                                    if (containsMouse) {
                                                        sidebar.setStatusHint(this, qsTr("No imported structure has a unit cell"))
                                                    } else {
                                                        sidebar.clearStatusHint(this)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }


                            SidebarBranchRow {
                                lastItem: true
                                content: Component {
                                    Item {
                                        Layout.fillWidth: true
                                        implicitHeight: unwrapButton.implicitHeight

                                        SidebarButton {
                                            id: unwrapButton
                                            text: qsTr("Unwrap Molecules")
                                            width: parent.width
                                            enabled: StructureModel.hasUnitCell && StructureModel.hasBonds && !StructureModel.editsLocked
                                            opacity: enabled ? 1.0 : 0.4
                                            onClicked: StructureModel.unwrapMolecules()
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            enabled: !(StructureModel.hasUnitCell && StructureModel.hasBonds)
                                            onContainsMouseChanged: {
                                                if (containsMouse) {
                                                    sidebar.setStatusHint(this, !StructureModel.hasUnitCell
                                                        ? qsTr("Not applicable for non-periodic structures")
                                                        : qsTr("Bond detection required"))
                                                } else {
                                                    sidebar.clearStatusHint(this)
                                                }
                                            }
                                        }
                                    }
                                }
                            }


                            SidebarBranchRow {
                                lastItem: true
                                content: Component {
                                    SidebarButton {
                                        text: qsTr("Reset to Original")
                                        Layout.fillWidth: true
                                        enabled: StructureModel.hasStructure && !StructureModel.editsLocked
                                        onClicked: {
                                            StructureModel.resetToOriginal()
                                        }
                                    }
                                }
                            }


                            SidebarBranchRow {
                                content: Component {
                                    SidebarCheckBox {
                                        text: qsTr("Show Strokes")
                                        checked: sidebar.viewport ? sidebar.viewport.outlineEnabled : true
                                        onCheckedChanged: {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.outlineEnabled = checked
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Stroke Thickness")
                                        statusHintTarget: sidebar
                                        tooltipText: qsTr("Stroke outline width in pixels along silhouettes and occluding boundaries.")
                                        from: 0.5
                                        to: 4
                                        stepSize: 0.1
                                        decimals: 1
                                        defaultValue: 1.0
                                        enabled: sidebar.viewport ? sidebar.viewport.outlineEnabled : true
                                        sourceValue: sidebar.viewport ? sidebar.viewport.outlineWidth : 1.0
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.outlineWidth = newValue
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                lastItem: true
                                content: Component {
                                    ColorPicker {
                                        pickerPopup: sharedColorPicker
                                        title: qsTr("Stroke Color")
                                        defaultColor: Qt.rgba(0, 0, 0, 1.0)
                                        sourceColor: sidebar.viewport ? sidebar.viewport.outlineColor : defaultColor
                                        onColorApplied: function(c) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.outlineColor = c
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                SidebarSection {
                    id: selectionSection
                    title: qsTr("Selection")
                    iconSource: "qrc:/icons/selection.svg"
                    Layout.fillWidth: true

                    content: Component {
                        ColumnLayout {
                            spacing: 8

                            SidebarBranchRow {
                                content: Component {
                                    ColumnLayout {
                                        spacing: 5
                                        Label {
                                            text: qsTr("Selection Range")
                                            color: sidebar.textStrong
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                        }
                                        SidebarComboBox {
                                            Layout.fillWidth: true
                                            model: [
                                                qsTr("No selection"),
                                                qsTr("Select atoms/bonds"),
                                                qsTr("Select molecules")
                                            ]
                                            currentIndex: StructureModel.selectionMode
                                            enabled: StructureModel.hasStructure
                                            onActivated: function(index) {
                                                StructureModel.selectionMode = index
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    SidebarButton {
                                        text: qsTr("Reset selected objects")
                                        Layout.fillWidth: true
                                        enabled: StructureModel.hasStructure
                                                 && StructureModel.selectionEnabled && !StructureModel.editsLocked
                                                 && (StructureModel.selectedAtomCount > 0
                                                     || StructureModel.selectedBondCount > 0)
                                        onClicked: {
                                            StructureModel.resetSelectedObjects(
                                                sidebar.viewport ? sidebar.viewport.bondRadius : 0.1,
                                                sidebar.viewport ? sidebar.viewport.atomColorScheme : 0)
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                lastItem: true
                                content: Component {
                                    SidebarButton {
                                        text: qsTr("Delete selected objects")
                                        Layout.fillWidth: true
                                        enabled: StructureModel.hasStructure
                                                 && StructureModel.selectionEnabled && !StructureModel.editsLocked
                                                 && (StructureModel.selectedAtomCount > 0
                                                     || StructureModel.selectedBondCount > 0)
                                        onClicked: StructureModel.deleteSelectedObjects()
                                    }
                                }
                            }
                        }
                    }
                }

                SidebarSection {
                    id: atomsSection
                    objectName: "atomsSection"
                    title: qsTr("Atoms")
                    iconSource: "qrc:/icons/atom.svg"
                    Layout.fillWidth: true

                    content: Component {
                        ColumnLayout {
                            spacing: 8

                            SidebarBranchRow {
                                content: Component {
                                    ColumnLayout {
                                        spacing: 5
                                        Label { text: qsTr("Atom radii"); color: sidebar.textStrong; font.pixelSize: 13; font.weight: Font.DemiBold }
                                        SidebarComboBox {
                                            objectName: "atomRadiiComboBox"
                                            Layout.fillWidth: true
                                            model: [qsTr("Covalent radii"), qsTr("Alvarez vdw radii")]
                                            currentIndex: StructureModel.atomRadiusType
                                            enabled: !StructureModel.switchingLocked
                                            onActivated: function(index) {
                                                StructureModel.atomRadiusType = index
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    ColumnLayout {
                                        spacing: 5
                                        Label { text: qsTr("Color Scheme"); color: sidebar.textStrong; font.pixelSize: 13; font.weight: Font.DemiBold }
                                        SidebarComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Jmol"), qsTr("CPK")]
                                            currentIndex: sidebar.viewport ? sidebar.viewport.atomColorScheme : 0
                                            onActivated: function(index) {
                                                if (sidebar.viewport) {
                                                    sidebar.viewport.atomColorScheme = index
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    ColorPicker {
                                        pickerPopup: sharedColorPicker
                                        selectionDependent: true
                                        enabled: StructureModel.selectionEnabled && StructureModel.selectedAtomCount > 0
                                        defaultColor: Qt.rgba(1.0, 1.0, 1.0, 1.0)
                                        sourceColor: StructureModel.selectedAtomColor
                                        onColorApplied: function(c) {
                                            StructureModel.applyAtomColorToSelection(c)
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Atom Scale")
                                        statusHintTarget: sidebar
                                        from: 0.1
                                        to: 2.0
                                        defaultValue: 1.0
                                        decimals: 2
                                        sourceValue: sidebar.viewport ? sidebar.viewport.atomScale : 1.0
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.atomScale = newValue
                                            }
                                        }
                                    }
                                }
                            }
                            SidebarBranchRow {
                                lastItem: true
                                content: Component {
                                    PerAtomProperties { pickerPopup: sharedColorPicker }
                                }
                            }
                        }
                    }
                }

                SidebarSection {
                    id: bondsSection
                    title: qsTr("Bonds")
                    iconSource: "qrc:/icons/bond.svg"
                    Layout.fillWidth: true

                    content: Component {
                        ColumnLayout {
                            spacing: 8

                            SidebarBranchRow {
                                content: Component {
                                    SidebarCheckBox {
                                        text: qsTr("Show Bonds")
                                        checked: sidebar.viewport ? sidebar.viewport.showBonds : false
                                        onCheckedChanged: {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.showBonds = checked
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Bond Radius")
                                        statusHintTarget: sidebar
                                        from: 0.01
                                        to: 0.6
                                        defaultValue: 0.1
                                        decimals: 2
                                        sourceValue: sidebar.viewport ? sidebar.viewport.bondRadius : 0.1
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.bondRadius = newValue
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                lastItem: true
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Neighborlist Cutoff Scale")
                                        statusHintTarget: sidebar
                                        from: 0.5
                                        to: 2.0
                                        defaultValue: 1.1
                                        decimals: 2
                                        sourceValue: sidebar.viewport ? sidebar.viewport.bondScale : 1.1
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.bondScale = newValue
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                SidebarSection {
                    id: unitCellSection
                    title: qsTr("Unit Cell")
                    iconSource: "qrc:/icons/unit-cell.svg"
                    Layout.fillWidth: true

                    content: Component {
                        ColumnLayout {
                            spacing: 8

                            SidebarBranchRow {
                                content: Component {
                                    SidebarCheckBox {
                                        text: qsTr("Show Unit Cell")
                                        checked: sidebar.viewport ? sidebar.viewport.showUnitCell : true
                                        onCheckedChanged: {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.showUnitCell = checked
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Thickness")
                                        statusHintTarget: sidebar
                                        from: 0.01
                                        to: 2.0
                                        defaultValue: 0.06
                                        decimals: 2
                                        sourceValue: sidebar.viewport ? sidebar.viewport.unitCellThickness : 0.06
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.unitCellThickness = newValue
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                lastItem: true
                                content: Component {
                                    ColorPicker {
                                        pickerPopup: sharedColorPicker
                                        defaultColor: Qt.rgba(0, 0, 0, 1.0)
                                        sourceColor: sidebar.viewport ? sidebar.viewport.unitCellColor : defaultColor
                                        onColorApplied: function(c) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.unitCellColor = c
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                SidebarSection {
                    id: cameraSection
                    title: qsTr("Camera")
                    iconSource: "qrc:/icons/camera.svg"
                    Layout.fillWidth: true

                    content: Component {
                        ColumnLayout {
                            spacing: 8

                            SidebarBranchRow {
                                content: Component {
                                    ColumnLayout {
                                        spacing: 5
                                        Label { text: qsTr("Projection"); color: sidebar.textStrong; font.pixelSize: 13; font.weight: Font.DemiBold }
                                        SidebarComboBox {
                                            Layout.fillWidth: true
                                            model: ["Perspective", "Orthographic"]
                                            currentIndex: sidebar.viewport ? (sidebar.viewport.isPerspective ? 0 : 1) : 0
                                            onActivated: function(index) {
                                                if (sidebar.viewport) {
                                                    sidebar.viewport.isPerspective = (index === 0)
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    ColumnLayout {
                                        spacing: 5
                                        Label { text: qsTr("Rotation constraint"); color: sidebar.textStrong; font.pixelSize: 13; font.weight: Font.DemiBold }
                                        SidebarComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("None"), qsTr("XY plane"), qsTr("YZ plane"), qsTr("XZ plane")]
                                            currentIndex: sidebar.viewport ? sidebar.viewport.rotationConstraint : 0
                                            onActivated: function(index) {
                                                if (sidebar.viewport) {
                                                    sidebar.viewport.rotationConstraint = index
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Field of View")
                                        statusHintTarget: sidebar
                                        tooltipText: qsTr("Perspective vertical field of view in degrees. A smaller value makes the structure appear larger.")
                                        integer: true
                                        from: 10
                                        to: 120
                                        stepSize: 1
                                        defaultValue: 45
                                        enabled: sidebar.viewport ? sidebar.viewport.isPerspective : true
                                        sourceValue: sidebar.viewport ? sidebar.viewport.fieldOfView : 45
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.fieldOfView = newValue
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Camera Distance")
                                        statusHintTarget: sidebar
                                        tooltipText: qsTr("Perspective distance from the camera target in structure coordinate units.")
                                        from: 0.1
                                        to: 100000
                                        stepSize: 0.1
                                        decimals: 2
                                        defaultValue: 50
                                        inputWidth: 76
                                        logarithmic: true
                                        enabled: sidebar.viewport ? sidebar.viewport.isPerspective : true
                                        sourceValue: sidebar.viewport ? sidebar.viewport.cameraDistance : 50
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.cameraDistance = newValue
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Orthographic Scale")
                                        statusHintTarget: sidebar
                                        tooltipText: qsTr("Orthographic visible half-height in structure coordinate units. A smaller value makes the structure appear larger.")
                                        from: 0.001
                                        to: 10000
                                        stepSize: 0.001
                                        decimals: 3
                                        defaultValue: 10
                                        inputWidth: 76
                                        logarithmic: true
                                        enabled: sidebar.viewport ? !sidebar.viewport.isPerspective : false
                                        sourceValue: sidebar.viewport ? sidebar.viewport.orthographicScale : 10
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.orthographicScale = newValue
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    SidebarCheckBox {
                                        text: qsTr("Auto-fit first structure and resets")
                                        checked: sidebar.viewport ? sidebar.viewport.autoFitOnLoad : true
                                        onToggled: {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.autoFitOnLoad = checked
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    ColumnLayout {
                                        spacing: 5
                                        Label { text: qsTr("View Direction"); color: sidebar.textStrong; font.pixelSize: 13; font.weight: Font.DemiBold }
                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: 2
                                            columnSpacing: 4
                                            rowSpacing: 4

                                            Repeater {
                                                model: ["+X", "−X", "+Y", "−Y", "+Z", "−Z"]
                                                delegate: SidebarButton {
                                                    text: modelData
                                                    Layout.fillWidth: true
                                                    onClicked: {
                                                        if (sidebar.viewport) {
                                                            sidebar.viewport.setViewDirection(index)
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }


                            SidebarBranchRow {
                                lastItem: true
                                content: Component {
                                    RowLayout {
                                        spacing: 6
                                        SidebarButton {
                                            text: qsTr("Reset Camera")
                                            Layout.fillWidth: true
                                            onClicked: {
                                                if (sidebar.viewport) {
                                                    sidebar.viewport.resetCamera()
                                                }
                                            }
                                        }
                                        SidebarButton {
                                            text: qsTr("Fit to View")
                                            Layout.fillWidth: true
                                            onClicked: {
                                                if (sidebar.viewport) {
                                                    sidebar.viewport.fitToView()
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                SidebarSection {
                    id: renderSettingsSection
                    title: qsTr("Render Settings")
                    iconSource: "qrc:/icons/render.svg"
                    Layout.fillWidth: true
                    expanded: false

                    content: Component {
                        ColumnLayout {
                            spacing: 8

                            SidebarBranchRow {
                                lastItem: !sidebar.rtSettingsVisible
                                content: Component {
                                    ColumnLayout {
                                        spacing: 5
                                        Label { text: qsTr("Render Mode"); color: sidebar.textStrong; font.pixelSize: 13; font.weight: Font.DemiBold }
                                        SidebarComboBox {
                                            Layout.fillWidth: true
                                            model: ["Raster (Fast)", "Ray Tracing (Quality)"]
                                            currentIndex: sidebar.viewport ? sidebar.viewport.rendererMode : 0
                                            onCurrentIndexChanged: {
                                                if (sidebar.viewport) {
                                                    sidebar.viewport.rendererMode = currentIndex
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                lastItem: !sidebar.rtSettingsVisible
                                visible: sidebar.rtSettingsVisible
                                content: Component {
                                    ColumnLayout {
                                        spacing: 5
                                        Label { text: qsTr("Max RT Samples"); color: sidebar.textStrong; font.pixelSize: 13; font.weight: Font.DemiBold }
                                        SidebarTextField {
                                            id: maxRTSamplesField
                                            Layout.fillWidth: true
                                            text: sidebar.viewport ? sidebar.viewport.maxRTSamples.toString() : "1000"
                                            placeholderText: qsTr("1000")
                                            hoverEnabled: true
                                            inputMethodHints: Qt.ImhDigitsOnly
                                            validator: IntValidator { bottom: 1; top: 10000 }
                                            onHoveredChanged: {
                                                if (hovered) {
                                                    sidebar.setStatusHint(this, qsTr("Enter any integer between 1 and 10000"))
                                                } else {
                                                    sidebar.clearStatusHint(this)
                                                }
                                            }

                                            onEditingFinished: {
                                                if (!sidebar.viewport) {
                                                    return
                                                }

                                                const rawText = text.trim()
                                                if (!/^\d+$/.test(rawText)) {
                                                    sidebar.showMaxRTSamplesError(qsTr("Invalid value. Enter an integer between 1 and 10000."))
                                                    text = sidebar.viewport.maxRTSamples.toString()
                                                    return
                                                }

                                                const parsed = Number(rawText)
                                                if (!Number.isInteger(parsed) || parsed < 1 || parsed > 10000) {
                                                    sidebar.showMaxRTSamplesError(qsTr("Invalid value. Enter an integer between 1 and 10000."))
                                                    text = sidebar.viewport.maxRTSamples.toString()
                                                    return
                                                }

                                                sidebar.viewport.maxRTSamples = parsed
                                                text = sidebar.viewport.maxRTSamples.toString()
                                            }

                                            Connections {
                                                target: sidebar.viewport
                                                ignoreUnknownSignals: true

                                                function onMaxRTSamplesChanged() {
                                                    if (sidebar.viewport) {
                                                        maxRTSamplesField.text = sidebar.viewport.maxRTSamples.toString()
                                                    }
                                                }

                                                function onRendererModeChanged() {
                                                    if (sidebar.rtSettingsVisible) {
                                                        maxRTSamplesField.text = sidebar.viewport.maxRTSamples.toString()
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }


                            SidebarBranchRow {
                                visible: sidebar.rtSettingsVisible
                                content: Component {
                                    SidebarCheckBox {
                                        text: qsTr("Ambient Occlusion")
                                        checked: sidebar.viewport ? sidebar.viewport.enableAO : false
                                        onCheckedChanged: {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.enableAO = checked
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                lastItem: true
                                visible: sidebar.rtSettingsVisible
                                content: Component {
                                    SidebarCheckBox {
                                        text: qsTr("Shadows")
                                        checked: sidebar.viewport ? sidebar.viewport.enableShadows : false
                                        onCheckedChanged: {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.enableShadows = checked
                                            }
                                        }
                                    }
                                }
                            }


                            SidebarBranchRow {
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Ambient")
                                        statusHintTarget: sidebar
                                        tooltipText: qsTr("Base light intensity applied everywhere. Higher values brighten the whole scene, including shadowed areas.")
                                        from: 0
                                        to: 1
                                        stepSize: 0.01
                                        decimals: 2
                                        defaultValue: 0.50
                                        sourceValue: sidebar.viewport ? sidebar.viewport.ambientStrength : 0.50
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.ambientStrength = newValue
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Diffuse")
                                        statusHintTarget: sidebar
                                        tooltipText: qsTr("Strength of directional matte lighting. Higher values increase light-facing contrast and shape definition.")
                                        from: 0
                                        to: 1
                                        stepSize: 0.01
                                        decimals: 2
                                        defaultValue: 0.7
                                        sourceValue: sidebar.viewport ? sidebar.viewport.diffuseStrength : 0.7
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.diffuseStrength = newValue
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Specular")
                                        statusHintTarget: sidebar
                                        tooltipText: qsTr("Brightness of reflective highlights. Higher values make highlights stronger and more noticeable.")
                                        from: 0
                                        to: 1
                                        stepSize: 0.01
                                        decimals: 2
                                        defaultValue: 0.05
                                        sourceValue: sidebar.viewport ? sidebar.viewport.specularStrength : 0.05
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.specularStrength = newValue
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                lastItem: true
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Shininess")
                                        statusHintTarget: sidebar
                                        tooltipText: qsTr("Sharpness of specular highlights. Higher values make highlights tighter; lower values make them softer.")
                                        integer: true
                                        from: 1
                                        to: 128
                                        stepSize: 1
                                        defaultValue: 60
                                        sourceValue: sidebar.viewport ? sidebar.viewport.shininess : 60
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.shininess = Math.round(newValue)
                                            }
                                        }
                                    }
                                }
                            }


                            SidebarBranchRow {
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Light azimuth")
                                        statusHintTarget: sidebar
                                        tooltipText: qsTr("Angle in the XY plane. 0° = +X, 90° = +Y, ±180° = −X.")
                                        from: -180
                                        to: 180
                                        stepSize: 1
                                        decimals: 0
                                        defaultValue: 0
                                        sourceValue: sidebar.viewport ? sidebar.viewport.lightAzimuth : 0
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.lightAzimuth = newValue
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                lastItem: true
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Light elevation")
                                        statusHintTarget: sidebar
                                        tooltipText: qsTr("Angle from the XY plane toward +Z. 0° = in XY plane, 90° = +Z, -90° = −Z.")
                                        from: -90
                                        to: 90
                                        stepSize: 1
                                        decimals: 0
                                        defaultValue: 45
                                        sourceValue: sidebar.viewport ? sidebar.viewport.lightElevation : 45
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.lightElevation = newValue
                                            }
                                        }
                                    }
                                }
                            }


                            SidebarBranchRow {
                                visible: sidebar.rtSettingsVisible
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Ambient occlusion samples")
                                        statusHintTarget: sidebar
                                        tooltipText: qsTr("Number of AO rays per pixel per frame. Higher values reduce AO noise but render slower.")
                                        integer: true
                                        from: 1
                                        to: 16
                                        stepSize: 1
                                        defaultValue: 4
                                        sourceValue: sidebar.viewport ? sidebar.viewport.aoSamples : 4
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.aoSamples = Math.round(newValue)
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                lastItem: true
                                visible: sidebar.rtSettingsVisible
                                content: Component {
                                    NumericSliderControl {
                                        title: qsTr("Ambient occlusion radius")
                                        statusHintTarget: sidebar
                                        tooltipText: qsTr("Maximum distance AO rays search for occluders. Higher values create broader occlusion effects.")
                                        from: 1
                                        to: 10
                                        stepSize: 0.1
                                        decimals: 1
                                        defaultValue: 3.0
                                        sourceValue: sidebar.viewport ? sidebar.viewport.aoRadius : 3.0
                                        onValueApplied: function(newValue) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.aoRadius = newValue
                                            }
                                        }
                                    }
                                }
                            }


                            SidebarBranchRow {
                                lastItem: true
                                content: Component {
                                    SidebarButton {
                                        text: qsTr("Reset Render Settings")
                                        Layout.fillWidth: true
                                        onClicked: sidebar.resetRayTracingSettings()
                                    }
                                }
                            }
                        }
                    }
                }

                SidebarSection {
                    id: backgroundSection
                    title: qsTr("Background")
                    iconSource: "qrc:/icons/background.svg"
                    Layout.fillWidth: true
                    expanded: false

                    content: Component {
                        ColumnLayout {
                            spacing: 8

                            SidebarBranchRow {
                                content: Component {
                                    ColorPicker {
                                        pickerPopup: sharedColorPicker
                                        showAlpha: true
                                        defaultColor: sidebar.defaultBackgroundColor
                                        sourceColor: sidebar.viewport ? sidebar.viewport.backgroundColor : defaultColor
                                        onColorApplied: function(c) {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.backgroundColor = c
                                            }
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                lastItem: true
                                content: Component {
                                    SidebarButton {
                                        text: qsTr("Reset to Default")
                                        Layout.fillWidth: true
                                        onClicked: {
                                            if (sidebar.viewport) {
                                                sidebar.viewport.backgroundColor = sidebar.defaultBackgroundColor
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                SidebarSection {
                    id: codeSection
                    objectName: "codeSection"
                    title: qsTr("Code")
                    iconSource: "qrc:/icons/code.svg"
                    Layout.fillWidth: true
                    content: Component {
                        ColumnLayout {
                            spacing: 8
                            SidebarBranchRow {
                                content: Component {
                                    SidebarButton {
                                        objectName: "openInteractiveShellButton"
                                        text: qsTr("Open Interactive Shell")
                                        Layout.fillWidth: true
                                        onClicked: sidebar.openInteractiveShell()
                                    }
                                }
                            }
                            SidebarBranchRow {
                                content: Component {
                                    SidebarButton {
                                        objectName: "shellTutorialButton"
                                        text: qsTr("Tutorial")
                                        Layout.fillWidth: true
                                        onClicked: sidebar.openShellTutorial()
                                    }
                                }
                            }
                            SidebarBranchRow {
                                content: Component {
                                    SidebarButton {
                                        objectName: "openPythonPackagesButton"
                                        text: qsTr("Manage Packages")
                                        Layout.fillWidth: true
                                        onClicked: sidebar.openPythonPackages()
                                    }
                                }
                            }
                            SidebarBranchRow {
                                lastItem: true
                                content: Component {
                                    SidebarButton {
                                        objectName: "openEnvironmentTerminalButton"
                                        text: qsTr("Open Environment Terminal")
                                        Layout.fillWidth: true
                                        onClicked: sidebar.openEnvironmentTerminal()
                                    }
                                }
                            }
                        }
                    }
                }

                SidebarSection {
                    id: quickGuideSection
                    title: qsTr("Quick Guide")
                    iconSource: "qrc:/icons/guide.svg"
                    Layout.fillWidth: true

                    content: Component {
                        ColumnLayout {
                            spacing: 8

                            SidebarBranchRow {
                                content: Component {
                                    ColumnLayout {
                                        spacing: 4

                                        Label {
                                            text: qsTr("Mouse Controls")
                                            color: sidebar.textStrong
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                        }

                                        Label {
                                            text: qsTr("Left mouse button: rotate the structure.")
                                            color: sidebar.textBody
                                            font.pixelSize: 13
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }

                                        Label {
                                            text: qsTr("Right mouse button: pan across the scene.")
                                            color: sidebar.textBody
                                            font.pixelSize: 13
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }

                                        Label {
                                            text: qsTr("Mouse wheel or trackpad scroll: zoom in or out.")
                                            color: sidebar.textBody
                                            font.pixelSize: 13
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }

                            SidebarBranchRow {
                                lastItem: true
                                content: Component {
                                    ColumnLayout {
                                        spacing: 4

                                        Label {
                                            text: qsTr("Common Actions")
                                            color: sidebar.textStrong
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                        }

                                        Label {
                                            text: qsTr("Double-click in the viewport to reset the camera view.")
                                            color: sidebar.textBody
                                            font.pixelSize: 13
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }

                                        Label {
                                            text: qsTr("Use the Camera tab to switch projection mode, constrain rotation, and jump to standard view directions.")
                                            color: sidebar.textBody
                                            font.pixelSize: 13
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }

                                        Label {
                                            text: qsTr("Use the Atoms, Bonds, and Unit Cell tabs to adjust what is shown and how the structure is rendered.")
                                            color: sidebar.textBody
                                            font.pixelSize: 13
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }

    Dialog {
        id: maxRTSamplesErrorDialog
        title: qsTr("Invalid Sample Limit")
        modal: true
        standardButtons: Dialog.Ok
        width: 340
        x: Math.round((sidebar.width - width) * 0.5)
        y: Math.round((sidebar.height - height) * 0.5)

        background: Rectangle {
            radius: 12
            color: sidebar.buttonFill
        }

        contentItem: Label {
            text: sidebar.maxRTSamplesErrorMessage
            width: 316
            color: sidebar.textBody
            wrapMode: Text.WordWrap
            padding: 12
        }
    }
}
