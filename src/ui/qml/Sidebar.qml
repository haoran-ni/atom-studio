import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AtomStudio 1.0

Rectangle {
    id: sidebar

    property var viewport: null
    property string maxRTSamplesErrorMessage: ""
    property string sliderInputErrorMessage: ""
    readonly property bool rtSettingsVisible: sidebar.viewport && sidebar.viewport.rendererMode === 1

    function showMaxRTSamplesError(message) {
        maxRTSamplesErrorMessage = message
        maxRTSamplesErrorDialog.open()
    }

    function showSliderInputError(message) {
        sliderInputErrorMessage = message
        sliderInputErrorDialog.open()
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
        sidebar.viewport.ambientStrength = 0.3
        sidebar.viewport.diffuseStrength = 0.7
        sidebar.viewport.specularStrength = 0.5
        sidebar.viewport.shininess = 32
        sidebar.viewport.lightDirX = 0.3
        sidebar.viewport.lightDirY = 0.8
        sidebar.viewport.lightDirZ = 0.5
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
                            defaultValue: 1.0
                            decimals: 2
                            sourceValue: sidebar.viewport ? sidebar.viewport.bondScale : 1.0
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

                        Label {
                            text: qsTr("RGB Color")
                            color: "#cccccc"
                            font.pixelSize: 11
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 24
                            radius: 2
                            border.color: "#3c3c3c"
                            border.width: 1
                            color: Qt.rgba(
                                unitCellRedControl.currentValue / 255.0,
                                unitCellGreenControl.currentValue / 255.0,
                                unitCellBlueControl.currentValue / 255.0,
                                1.0
                            )
                        }

                        NumericSliderControl {
                            id: unitCellRedControl
                            title: qsTr("R")
                            titleColor: "#ff7777"
                            titlePixelSize: 10
                            integer: true
                            from: 0
                            to: 255
                            stepSize: 1
                            defaultValue: 0
                            sourceValue: sidebar.viewport ? Math.round(sidebar.viewport.unitCellColor.r * 255) : 0
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.unitCellColor = Qt.rgba(
                                        newValue / 255.0,
                                        unitCellGreenControl.currentValue / 255.0,
                                        unitCellBlueControl.currentValue / 255.0,
                                        1.0
                                    )
                                }
                            }
                        }

                        NumericSliderControl {
                            id: unitCellGreenControl
                            title: qsTr("G")
                            titleColor: "#77ff77"
                            titlePixelSize: 10
                            integer: true
                            from: 0
                            to: 255
                            stepSize: 1
                            defaultValue: 0
                            sourceValue: sidebar.viewport ? Math.round(sidebar.viewport.unitCellColor.g * 255) : 0
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.unitCellColor = Qt.rgba(
                                        unitCellRedControl.currentValue / 255.0,
                                        newValue / 255.0,
                                        unitCellBlueControl.currentValue / 255.0,
                                        1.0
                                    )
                                }
                            }
                        }

                        NumericSliderControl {
                            id: unitCellBlueControl
                            title: qsTr("B")
                            titleColor: "#7777ff"
                            titlePixelSize: 10
                            integer: true
                            from: 0
                            to: 255
                            stepSize: 1
                            defaultValue: 0
                            sourceValue: sidebar.viewport ? Math.round(sidebar.viewport.unitCellColor.b * 255) : 0
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.unitCellColor = Qt.rgba(
                                        unitCellRedControl.currentValue / 255.0,
                                        unitCellGreenControl.currentValue / 255.0,
                                        newValue / 255.0,
                                        1.0
                                    )
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
                            currentIndex: 0
                        }

                        NumericSliderControl {
                            title: qsTr("Field of View")
                            integer: true
                            from: 30
                            to: 120
                            stepSize: 1
                            defaultValue: 45
                            sourceValue: 45
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

                        NumericSliderControl {
                            title: qsTr("Ambient")
                            tooltipText: qsTr("Base light intensity applied everywhere. Higher values brighten the whole scene, including shadowed areas.")
                            from: 0
                            to: 1
                            stepSize: 0.01
                            decimals: 2
                            defaultValue: 0.3
                            visible: sidebar.rtSettingsVisible
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
                            visible: sidebar.rtSettingsVisible
                            sourceValue: sidebar.viewport ? sidebar.viewport.diffuseStrength : 0.7
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.diffuseStrength = newValue
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
                            defaultValue: 0.5
                            visible: sidebar.rtSettingsVisible
                            sourceValue: sidebar.viewport ? sidebar.viewport.specularStrength : 0.5
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
                            visible: sidebar.rtSettingsVisible
                            sourceValue: sidebar.viewport ? sidebar.viewport.shininess : 32
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.shininess = Math.round(newValue)
                                }
                            }
                        }

                        NumericSliderControl {
                            title: qsTr("Light direction (X)")
                            from: -1
                            to: 1
                            stepSize: 0.01
                            decimals: 2
                            defaultValue: 0.3
                            visible: sidebar.rtSettingsVisible
                            sourceValue: sidebar.viewport ? sidebar.viewport.lightDirX : 0.3
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.lightDirX = newValue
                                }
                            }
                        }

                        NumericSliderControl {
                            title: qsTr("Light direction (Y)")
                            from: -1
                            to: 1
                            stepSize: 0.01
                            decimals: 2
                            defaultValue: 0.8
                            visible: sidebar.rtSettingsVisible
                            sourceValue: sidebar.viewport ? sidebar.viewport.lightDirY : 0.8
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.lightDirY = newValue
                                }
                            }
                        }

                        NumericSliderControl {
                            title: qsTr("Light direction (Z)")
                            from: -1
                            to: 1
                            stepSize: 0.01
                            decimals: 2
                            defaultValue: 0.5
                            visible: sidebar.rtSettingsVisible
                            sourceValue: sidebar.viewport ? sidebar.viewport.lightDirZ : 0.5
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.lightDirZ = newValue
                                }
                            }
                        }

                        Button {
                            text: qsTr("Reset RT Settings")
                            Layout.fillWidth: true
                            visible: sidebar.rtSettingsVisible
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

                        Label {
                            text: qsTr("RGB Color")
                            color: "#cccccc"
                            font.pixelSize: 11
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 24
                            radius: 2
                            border.color: "#3c3c3c"
                            border.width: 1
                            color: Qt.rgba(
                                backgroundRedControl.currentValue / 255.0,
                                backgroundGreenControl.currentValue / 255.0,
                                backgroundBlueControl.currentValue / 255.0,
                                1.0
                            )
                        }

                        NumericSliderControl {
                            id: backgroundRedControl
                            title: qsTr("R")
                            titleColor: "#ff7777"
                            titlePixelSize: 10
                            integer: true
                            from: 0
                            to: 255
                            stepSize: 1
                            defaultValue: 230
                            sourceValue: sidebar.viewport ? Math.round(sidebar.viewport.backgroundColor.r * 255) : 230
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.backgroundColor = Qt.rgba(
                                        newValue / 255.0,
                                        backgroundGreenControl.currentValue / 255.0,
                                        backgroundBlueControl.currentValue / 255.0,
                                        1.0
                                    )
                                }
                            }
                        }

                        NumericSliderControl {
                            id: backgroundGreenControl
                            title: qsTr("G")
                            titleColor: "#77ff77"
                            titlePixelSize: 10
                            integer: true
                            from: 0
                            to: 255
                            stepSize: 1
                            defaultValue: 230
                            sourceValue: sidebar.viewport ? Math.round(sidebar.viewport.backgroundColor.g * 255) : 230
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.backgroundColor = Qt.rgba(
                                        backgroundRedControl.currentValue / 255.0,
                                        newValue / 255.0,
                                        backgroundBlueControl.currentValue / 255.0,
                                        1.0
                                    )
                                }
                            }
                        }

                        NumericSliderControl {
                            id: backgroundBlueControl
                            title: qsTr("B")
                            titleColor: "#7777ff"
                            titlePixelSize: 10
                            integer: true
                            from: 0
                            to: 255
                            stepSize: 1
                            defaultValue: 230
                            sourceValue: sidebar.viewport ? Math.round(sidebar.viewport.backgroundColor.b * 255) : 230
                            onValueApplied: function(newValue) {
                                if (sidebar.viewport) {
                                    sidebar.viewport.backgroundColor = Qt.rgba(
                                        backgroundRedControl.currentValue / 255.0,
                                        backgroundGreenControl.currentValue / 255.0,
                                        newValue / 255.0,
                                        1.0
                                    )
                                }
                            }
                        }

                        Button {
                            text: qsTr("Reset to Default")
                            Layout.fillWidth: true
                            onClicked: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.backgroundColor = Qt.rgba(
                                        230 / 255.0,
                                        230 / 255.0,
                                        230 / 255.0,
                                        1.0
                                    )
                                } else {
                                    backgroundRedControl.currentValue = 230
                                    backgroundGreenControl.currentValue = 230
                                    backgroundBlueControl.currentValue = 230
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

    Dialog {
        id: sliderInputErrorDialog
        title: qsTr("Invalid Value")
        modal: true
        standardButtons: Dialog.Ok
        width: 340
        x: Math.round((sidebar.width - width) * 0.5)
        y: Math.round((sidebar.height - height) * 0.5)

        contentItem: Label {
            text: sidebar.sliderInputErrorMessage
            width: 316
            color: "#cccccc"
            wrapMode: Text.WordWrap
            padding: 12
        }
    }

    component NumericSliderControl: ColumnLayout {
        id: control

        property string title: ""
        property string tooltipText: ""
        property color titleColor: "#cccccc"
        property int titlePixelSize: 11
        property bool integer: false
        property int decimals: integer ? 0 : 2
        property real defaultValue: 0
        property real sourceValue: defaultValue
        property int inputWidth: 74

        property alias from: slider.from
        property alias to: slider.to
        property alias stepSize: slider.stepSize
        property alias currentValue: slider.value

        signal valueApplied(real newValue)

        Layout.fillWidth: true
        spacing: 4

        function normalizedValue(rawValue) {
            if (!Number.isFinite(rawValue)) {
                return defaultValue
            }
            return integer ? Math.round(rawValue) : rawValue
        }

        function formatValue(rawValue) {
            const normalized = normalizedValue(rawValue)
            if (integer) {
                return normalized.toString()
            }
            return Number(normalized).toFixed(decimals)
        }

        function errorMessage() {
            if (integer) {
                return qsTr("Invalid value. Enter an integer between %1 and %2.")
                    .arg(Math.round(slider.from))
                    .arg(Math.round(slider.to))
            }

            return qsTr("Invalid value. Enter a number between %1 and %2.")
                .arg(Number(slider.from).toFixed(decimals))
                .arg(Number(slider.to).toFixed(decimals))
        }

        function revertText() {
            valueField.text = formatValue(slider.value)
        }

        function applyValue(newValue) {
            const normalized = normalizedValue(newValue)
            slider.value = normalized
            valueField.text = formatValue(slider.value)
            valueApplied(normalized)
        }

        function commitText() {
            const rawText = valueField.text.trim()

            if (rawText.length === 0) {
                sidebar.showSliderInputError(errorMessage())
                revertText()
                return
            }

            let parsed = NaN
            if (integer) {
                if (!/^[+-]?\d+$/.test(rawText)) {
                    sidebar.showSliderInputError(errorMessage())
                    revertText()
                    return
                }
                parsed = Number(rawText)
                if (!Number.isInteger(parsed)) {
                    sidebar.showSliderInputError(errorMessage())
                    revertText()
                    return
                }
            } else {
                parsed = Number(rawText)
                if (!Number.isFinite(parsed)) {
                    sidebar.showSliderInputError(errorMessage())
                    revertText()
                    return
                }
            }

            if (parsed < slider.from || parsed > slider.to) {
                sidebar.showSliderInputError(errorMessage())
                revertText()
                return
            }

            applyValue(parsed)
        }

        onSourceValueChanged: {
            if (!valueField.activeFocus) {
                slider.value = normalizedValue(sourceValue)
                valueField.text = formatValue(slider.value)
            }
        }

        Component.onCompleted: {
            slider.value = normalizedValue(sourceValue)
            valueField.text = formatValue(slider.value)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                id: titleLabel
                text: control.title
                color: control.titleColor
                font.pixelSize: control.titlePixelSize
                Layout.fillWidth: true
                elide: Text.ElideRight
                ToolTip.visible: control.tooltipText.length > 0 && titleHoverArea.containsMouse
                ToolTip.text: control.tooltipText

                MouseArea {
                    id: titleHoverArea
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    hoverEnabled: control.tooltipText.length > 0
                    enabled: control.tooltipText.length > 0
                }
            }

            TextField {
                id: valueField
                Layout.preferredWidth: control.inputWidth
                horizontalAlignment: TextInput.AlignRight
                inputMethodHints: control.integer ? Qt.ImhDigitsOnly : Qt.ImhFormattedNumbersOnly
                selectByMouse: true
                onAccepted: control.commitText()
            }

            Button {
                text: qsTr("Reset")
                onClicked: control.applyValue(control.defaultValue)
            }
        }

        Slider {
            id: slider
            Layout.fillWidth: true
            onValueChanged: control.revertText()
            onMoved: control.valueApplied(control.normalizedValue(value))
        }
    }

    // Collapsible Section Component
    component CollapsibleSection: ColumnLayout {
        id: section

        property string title: ""
        property alias content: contentLoader.sourceComponent
        property bool expanded: false

        spacing: 0
        Layout.fillWidth: true

        // Section header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: sectionMouse.containsMouse ? "#3c3c3c" : "#333333"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Label {
                    text: section.expanded ? "\u25BC" : "\u25B6"
                    color: "#808080"
                    font.pixelSize: 10
                }

                Label {
                    text: section.title
                    color: "#cccccc"
                    font.pixelSize: 11
                    font.bold: true
                    Layout.fillWidth: true
                }
            }

            MouseArea {
                id: sectionMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: section.expanded = !section.expanded
            }
        }

        // Section content
        Loader {
            id: contentLoader
            Layout.fillWidth: true
            Layout.leftMargin: 15
            Layout.rightMargin: 10
            Layout.topMargin: 8
            Layout.bottomMargin: 8
            visible: section.expanded
        }
    }

    // Property Row Component
    component PropertyRow: RowLayout {
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
}
