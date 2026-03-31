import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AtomStudio 1.0

Rectangle {
    id: sidebar

    property var viewport: null
    property string maxRTSamplesErrorMessage: ""
    readonly property bool rtSettingsVisible: sidebar.viewport && sidebar.viewport.rendererMode === 1

    function showMaxRTSamplesError(message) {
        maxRTSamplesErrorMessage = message
        maxRTSamplesErrorDialog.open()
    }

    function resetRayTracingSettings() {
        if (!sidebar.viewport) {
            return
        }

        sidebar.viewport.maxRTSamples = 1000
        sidebar.viewport.enableAO = false
        sidebar.viewport.enableShadows = false
        sidebar.viewport.shadowOpacity = 1.0
        sidebar.viewport.aoSamples = 4
        sidebar.viewport.aoRadius = 3.0
        sidebar.viewport.ambientStrength = 0.3
        sidebar.viewport.diffuseStrength = 0.7
        sidebar.viewport.specularStrength = 0.0
        sidebar.viewport.shininess = 32
        sidebar.viewport.lightAzimuth = 0
        sidebar.viewport.lightElevation = 45
    }

    color: "#252526"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        // Sidebar header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: "#2d2d2d"

            Label {
                anchors.centerIn: parent
                text: qsTr("Properties")
                color: "#cccccc"
                font.pixelSize: 12
                font.bold: true
            }
        }

        // Separator
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: "#3c3c3c"
        }

        // Scrollable content area
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: sidebar.width
                spacing: 1

                // Structure Info Section
                CollapsibleSection {
                    title: qsTr("Structure Info")
                    Layout.fillWidth: true

                    content: ColumnLayout {
                        spacing: 8

                        PropertyRow {
                            label: qsTr("File:")
                            value: StructureModel.fileName
                        }

                        PropertyRow {
                            label: qsTr("Atoms:")
                            value: StructureModel.atomCount.toString()
                        }

                        PropertyRow {
                            label: qsTr("Atom Types:")
                            value: StructureModel.atomTypeCount.toString()
                        }

                        PropertyRow {
                            label: qsTr("Bonds:")
                            value: StructureModel.bondCount.toString()
                        }

                        // Element list
                        Repeater {
                            model: StructureModel.elements
                            delegate: PropertyRow {
                                label: ""
                                value: modelData
                            }
                        }

                        // Unit cell info
                        PropertyRow {
                            visible: StructureModel.hasUnitCell
                            label: qsTr("Unit Cell:")
                            value: StructureModel.hasUnitCell ? "Yes" : "No"
                        }

                        Label {
                            visible: StructureModel.hasUnitCell
                            text: StructureModel.cellParameters
                            color: "#cccccc"
                            font.pixelSize: 10
                            Layout.fillWidth: true
                            Layout.leftMargin: 85
                        }
                    }
                }

                // Structure Manipulation Section
                CollapsibleSection {
                    title: qsTr("Structure Manipulation")
                    Layout.fillWidth: true

                    content: ColumnLayout {
                        spacing: 8

                        // ---- Replicate Unit Cell ----
                        Label {
                            text: qsTr("Replicate Unit Cell")
                            color: "#cccccc"
                            font.pixelSize: 11
                        }

                        // Controls + disabled overlay
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: replicateControls.implicitHeight

                            ColumnLayout {
                                id: replicateControls
                                width: parent.width
                                spacing: 6
                                enabled: StructureModel.hasUnitCell
                                opacity: enabled ? 1.0 : 0.4

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Label {
                                        text: qsTr("X:")
                                        color: "#cccccc"
                                        font.pixelSize: 11
                                        Layout.preferredWidth: 16
                                    }
                                    TextField {
                                        id: replicateX
                                        Layout.fillWidth: true
                                        text: "1"
                                        horizontalAlignment: TextInput.AlignHCenter
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        validator: IntValidator { bottom: 1; top: 99 }
                                        selectByMouse: true
                                    }

                                    Label {
                                        text: qsTr("Y:")
                                        color: "#cccccc"
                                        font.pixelSize: 11
                                        Layout.preferredWidth: 16
                                    }
                                    TextField {
                                        id: replicateY
                                        Layout.fillWidth: true
                                        text: "1"
                                        horizontalAlignment: TextInput.AlignHCenter
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        validator: IntValidator { bottom: 1; top: 99 }
                                        selectByMouse: true
                                    }

                                    Label {
                                        text: qsTr("Z:")
                                        color: "#cccccc"
                                        font.pixelSize: 11
                                        Layout.preferredWidth: 16
                                    }
                                    TextField {
                                        id: replicateZ
                                        Layout.fillWidth: true
                                        text: "1"
                                        horizontalAlignment: TextInput.AlignHCenter
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        validator: IntValidator { bottom: 1; top: 99 }
                                        selectByMouse: true
                                    }
                                }

                                Button {
                                    text: qsTr("Apply Replication")
                                    Layout.fillWidth: true
                                    onClicked: {
                                        const nx = parseInt(replicateX.text)
                                        const ny = parseInt(replicateY.text)
                                        const nz = parseInt(replicateZ.text)
                                        if (!Number.isInteger(nx) || nx < 1 ||
                                            !Number.isInteger(ny) || ny < 1 ||
                                            !Number.isInteger(nz) || nz < 1) {
                                            sidebar.showSliderInputError(
                                                qsTr("Replication factors must be integers \u2265 1."))
                                            return
                                        }
                                        StructureModel.replicateCell(nx, ny, nz)
                                    }
                                }
                            }

                            // Transparent overlay: captures hover when disabled to show tooltip
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: !StructureModel.hasUnitCell
                                ToolTip.visible: containsMouse
                                ToolTip.text: qsTr("Not applicable for non-periodic structures")
                            }
                        }

                        // ---- Unwrap Molecules ----
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: unwrapButton.implicitHeight

                            Button {
                                id: unwrapButton
                                text: qsTr("Unwrap Molecules")
                                width: parent.width
                                enabled: StructureModel.hasUnitCell && StructureModel.hasBonds
                                opacity: enabled ? 1.0 : 0.4
                                onClicked: StructureModel.unwrapMolecules()
                            }

                            // Transparent overlay for tooltip when disabled
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: !(StructureModel.hasUnitCell && StructureModel.hasBonds)
                                ToolTip.visible: containsMouse
                                ToolTip.text: !StructureModel.hasUnitCell
                                    ? qsTr("Not applicable for non-periodic structures")
                                    : qsTr("Bond detection required")
                            }
                        }

                        // ---- Reset ----
                        Button {
                            text: qsTr("Reset to Original")
                            Layout.fillWidth: true
                            enabled: StructureModel.hasStructure
                            onClicked: StructureModel.resetToOriginal()
                        }
                    }
                }

                // Visualization Section
                CollapsibleSection {
                    title: qsTr("Visualization")
                    Layout.fillWidth: true

                    content: ColumnLayout {
                        spacing: 8

                        Label {
                            text: qsTr("Atom Style")
                            color: "#cccccc"
                            font.pixelSize: 11
                        }

                        ComboBox {
                            Layout.fillWidth: true
                            model: ["Sphere", "Ball & Stick", "CPK", "Wireframe"]
                            currentIndex: 0
                        }

                        Label {
                            text: qsTr("Color Scheme")
                            color: "#cccccc"
                            font.pixelSize: 11
                        }

                        ComboBox {
                            Layout.fillWidth: true
                            model: [qsTr("Jmol"), qsTr("CPK")]
                            currentIndex: sidebar.viewport ? sidebar.viewport.atomColorScheme : 0
                            onActivated: function(index) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.atomColorScheme = index
                                }
                            }
                        }

                        NumericSliderControl {
                            title: qsTr("Atom Scale")
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

                        NumericSliderControl {
                            title: qsTr("Bond Scale")
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

                        CheckBox {
                            id: showBondsCheck
                            text: qsTr("Show Bonds")
                            checked: true
                            onCheckedChanged: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.showBonds = checked
                                }
                            }
                        }

                    }
                }

                // Unit Cell Section
                CollapsibleSection {
                    title: qsTr("Unit Cell")
                    Layout.fillWidth: true

                    content: ColumnLayout {
                        spacing: 8

                        CheckBox {
                            text: qsTr("Show Unit Cell")
                            checked: sidebar.viewport ? sidebar.viewport.showUnitCell : true
                            onCheckedChanged: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.showUnitCell = checked
                                }
                            }
                        }

                        NumericSliderControl {
                            title: qsTr("Thickness")
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

                        RGBColorPicker {
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

                // Camera Section
                CollapsibleSection {
                    title: qsTr("Camera")
                    Layout.fillWidth: true

                    content: ColumnLayout {
                        spacing: 8

                        Label {
                            text: qsTr("Projection")
                            color: "#cccccc"
                            font.pixelSize: 11
                        }

                        ComboBox {
                            Layout.fillWidth: true
                            model: ["Perspective", "Orthographic"]
                            currentIndex: sidebar.viewport ? (sidebar.viewport.isPerspective ? 0 : 1) : 0
                            onActivated: function(index) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.isPerspective = (index === 0)
                                }
                            }
                        }

                        NumericSliderControl {
                            title: qsTr("Field of View")
                            visible: sidebar.viewport ? sidebar.viewport.isPerspective : true
                            integer: true
                            from: 10
                            to: 120
                            stepSize: 1
                            defaultValue: 45
                            sourceValue: sidebar.viewport ? sidebar.viewport.fieldOfView : 45
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.fieldOfView = newValue
                                }
                            }
                        }

                        Label {
                            text: qsTr("View Direction")
                            color: "#cccccc"
                            font.pixelSize: 11
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 4
                            rowSpacing: 4

                            Repeater {
                                model: ["+X", "−X", "+Y", "−Y", "+Z", "−Z"]
                                Button {
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

                        Button {
                            text: qsTr("Reset Camera")
                            Layout.fillWidth: true
                            onClicked: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.resetCamera()
                                }
                            }
                        }

                        Button {
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

                // Render Settings Section
                CollapsibleSection {
                    title: qsTr("Render Settings")
                    Layout.fillWidth: true
                    expanded: false

                    content: ColumnLayout {
                        spacing: 8

                        Label {
                            text: qsTr("Render Mode")
                            color: "#cccccc"
                            font.pixelSize: 11
                        }

                        ComboBox {
                            Layout.fillWidth: true
                            model: ["Raster (Fast)", "Ray Tracing (Quality)"]
                            currentIndex: sidebar.viewport ? sidebar.viewport.rendererMode : 0
                            onCurrentIndexChanged: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.rendererMode = currentIndex
                                }
                            }
                        }

                        Label {
                            text: qsTr("Max RT Samples")
                            color: "#cccccc"
                            font.pixelSize: 11
                            visible: sidebar.rtSettingsVisible
                        }

                        TextField {
                            id: maxRTSamplesField
                            Layout.fillWidth: true
                            visible: sidebar.rtSettingsVisible
                            text: sidebar.viewport ? sidebar.viewport.maxRTSamples.toString() : "1000"
                            placeholderText: qsTr("1000")
                            hoverEnabled: true
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 1; top: 10000 }
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Enter any integer between 1 and 10000")

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

                        CheckBox {
                            text: qsTr("Ambient Occlusion")
                            visible: sidebar.rtSettingsVisible
                            checked: sidebar.viewport ? sidebar.viewport.enableAO : false
                            onCheckedChanged: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.enableAO = checked
                                }
                            }
                        }

                        CheckBox {
                            text: qsTr("Shadows")
                            visible: sidebar.rtSettingsVisible
                            checked: sidebar.viewport ? sidebar.viewport.enableShadows : false
                            onCheckedChanged: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.enableShadows = checked
                                }
                            }
                        }

                        NumericSliderControl {
                            title: qsTr("Ambient")
                            tooltipText: qsTr("Base light intensity applied everywhere. Higher values brighten the whole scene, including shadowed areas.")
                            from: 0
                            to: 1
                            stepSize: 0.01
                            decimals: 2
                            defaultValue: 0.3
                            sourceValue: sidebar.viewport ? sidebar.viewport.ambientStrength : 0.3
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.ambientStrength = newValue
                                }
                            }
                        }

                        NumericSliderControl {
                            title: qsTr("Diffuse")
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

                        NumericSliderControl {
                            title: qsTr("Shadow opacity")
                            tooltipText: qsTr("Strength of ray-traced shadows. 0 disables shadow darkening, 1 keeps fully dark shadows.")
                            from: 0
                            to: 1
                            stepSize: 0.01
                            decimals: 2
                            defaultValue: 1.0
                            visible: sidebar.rtSettingsVisible
                            enabled: sidebar.viewport ? sidebar.viewport.enableShadows : false
                            sourceValue: sidebar.viewport ? sidebar.viewport.shadowOpacity : 1.0
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.shadowOpacity = newValue
                                }
                            }
                        }

                        NumericSliderControl {
                            title: qsTr("Specular")
                            tooltipText: qsTr("Brightness of reflective highlights. Higher values make highlights stronger and more noticeable.")
                            from: 0
                            to: 1
                            stepSize: 0.01
                            decimals: 2
                            defaultValue: 0.0
                            sourceValue: sidebar.viewport ? sidebar.viewport.specularStrength : 0.0
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.specularStrength = newValue
                                }
                            }
                        }

                        NumericSliderControl {
                            title: qsTr("Shininess")
                            tooltipText: qsTr("Sharpness of specular highlights. Higher values make highlights tighter; lower values make them softer.")
                            integer: true
                            from: 1
                            to: 128
                            stepSize: 1
                            defaultValue: 32
                            sourceValue: sidebar.viewport ? sidebar.viewport.shininess : 32
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.shininess = Math.round(newValue)
                                }
                            }
                        }

                        NumericSliderControl {
                            title: qsTr("Light azimuth")
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

                        NumericSliderControl {
                            title: qsTr("Light elevation")
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

                        NumericSliderControl {
                            title: qsTr("Ambient occlusion samples")
                            tooltipText: qsTr("Number of AO rays per pixel per frame. Higher values reduce AO noise but render slower.")
                            integer: true
                            from: 1
                            to: 16
                            stepSize: 1
                            defaultValue: 4
                            visible: sidebar.rtSettingsVisible
                            sourceValue: sidebar.viewport ? sidebar.viewport.aoSamples : 4
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.aoSamples = Math.round(newValue)
                                }
                            }
                        }

                        NumericSliderControl {
                            title: qsTr("Ambient occlusion radius")
                            tooltipText: qsTr("Maximum distance AO rays search for occluders. Higher values create broader occlusion effects.")
                            from: 1
                            to: 10
                            stepSize: 0.1
                            decimals: 1
                            defaultValue: 3.0
                            visible: sidebar.rtSettingsVisible
                            sourceValue: sidebar.viewport ? sidebar.viewport.aoRadius : 3.0
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.aoRadius = newValue
                                }
                            }
                        }

                        Button {
                            text: qsTr("Reset Render Settings")
                            Layout.fillWidth: true
                            onClicked: sidebar.resetRayTracingSettings()
                        }
                    }
                }

                // Background Section
                CollapsibleSection {
                    title: qsTr("Background")
                    Layout.fillWidth: true
                    expanded: false

                    content: ColumnLayout {
                        spacing: 8

                        RGBColorPicker {
                            id: backgroundColorPicker
                            defaultColor: Qt.rgba(230 / 255.0, 230 / 255.0, 230 / 255.0, 1.0)
                            sourceColor: sidebar.viewport ? sidebar.viewport.backgroundColor : defaultColor
                            onColorApplied: function(c) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.backgroundColor = c
                                }
                            }
                        }

                        Button {
                            text: qsTr("Reset to Default")
                            Layout.fillWidth: true
                            onClicked: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.backgroundColor = backgroundColorPicker.defaultColor
                                }
                            }
                        }
                    }
                }

                // Spacer to push content to top
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

        contentItem: Label {
            text: sidebar.maxRTSamplesErrorMessage
            width: 316
            color: "#cccccc"
            wrapMode: Text.WordWrap
            padding: 12
        }
    }

}
