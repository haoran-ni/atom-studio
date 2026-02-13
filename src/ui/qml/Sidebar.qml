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

                        Label {
                            text: qsTr("Atom Scale")
                            color: "#cccccc"
                            font.pixelSize: 11
                        }

                        Slider {
                            id: atomScaleSlider
                            Layout.fillWidth: true
                            from: 0.1
                            to: 2.0
                            value: 1.0
                            onValueChanged: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.atomScale = value
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

                        Label {
                            text: qsTr("Thickness")
                            color: "#cccccc"
                            font.pixelSize: 11
                        }

                        Slider {
                            id: unitCellThicknessSlider
                            Layout.fillWidth: true
                            from: 0.01
                            to: 2.0
                            value: sidebar.viewport ? sidebar.viewport.unitCellThickness : 0.06
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.unitCellThickness = value
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
                                unitCellRedSlider.value / 255.0,
                                unitCellGreenSlider.value / 255.0,
                                unitCellBlueSlider.value / 255.0,
                                1.0
                            )
                        }

                        Label {
                            text: qsTr("R")
                            color: "#ff7777"
                            font.pixelSize: 10
                        }

                        Slider {
                            id: unitCellRedSlider
                            Layout.fillWidth: true
                            from: 0
                            to: 255
                            stepSize: 1
                            value: sidebar.viewport ? Math.round(sidebar.viewport.unitCellColor.r * 255) : 0
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.unitCellColor = Qt.rgba(
                                        value / 255.0,
                                        unitCellGreenSlider.value / 255.0,
                                        unitCellBlueSlider.value / 255.0,
                                        1.0
                                    )
                                }
                            }
                        }

                        Label {
                            text: qsTr("G")
                            color: "#77ff77"
                            font.pixelSize: 10
                        }

                        Slider {
                            id: unitCellGreenSlider
                            Layout.fillWidth: true
                            from: 0
                            to: 255
                            stepSize: 1
                            value: sidebar.viewport ? Math.round(sidebar.viewport.unitCellColor.g * 255) : 0
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.unitCellColor = Qt.rgba(
                                        unitCellRedSlider.value / 255.0,
                                        value / 255.0,
                                        unitCellBlueSlider.value / 255.0,
                                        1.0
                                    )
                                }
                            }
                        }

                        Label {
                            text: qsTr("B")
                            color: "#7777ff"
                            font.pixelSize: 10
                        }

                        Slider {
                            id: unitCellBlueSlider
                            Layout.fillWidth: true
                            from: 0
                            to: 255
                            stepSize: 1
                            value: sidebar.viewport ? Math.round(sidebar.viewport.unitCellColor.b * 255) : 0
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.unitCellColor = Qt.rgba(
                                        unitCellRedSlider.value / 255.0,
                                        unitCellGreenSlider.value / 255.0,
                                        value / 255.0,
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

                        Label {
                            text: qsTr("Field of View")
                            color: "#cccccc"
                            font.pixelSize: 11
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 30
                            to: 120
                            value: 45
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

                        Label {
                            id: aoSamplesLabel
                            text: qsTr("Ambient occlusion samples: %1").arg(sidebar.viewport ? sidebar.viewport.aoSamples : 4)
                            color: "#cccccc"
                            font.pixelSize: 11
                            visible: sidebar.rtSettingsVisible
                            ToolTip.visible: aoSamplesHoverArea.containsMouse
                            ToolTip.text: qsTr("Number of AO rays per pixel per frame. Higher values reduce AO noise but render slower.")

                            MouseArea {
                                id: aoSamplesHoverArea
                                anchors.fill: parent
                                acceptedButtons: Qt.NoButton
                                hoverEnabled: true
                            }
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 1
                            to: 16
                            stepSize: 1
                            visible: sidebar.rtSettingsVisible
                            value: sidebar.viewport ? sidebar.viewport.aoSamples : 4
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.aoSamples = Math.round(value)
                                }
                            }
                        }

                        Label {
                            id: aoRadiusLabel
                            text: qsTr("Ambient occlusion radius: %1").arg(sidebar.viewport ? sidebar.viewport.aoRadius.toFixed(1) : "3.0")
                            color: "#cccccc"
                            font.pixelSize: 11
                            visible: sidebar.rtSettingsVisible
                            ToolTip.visible: aoRadiusHoverArea.containsMouse
                            ToolTip.text: qsTr("Maximum distance AO rays search for occluders. Higher values create broader occlusion effects.")

                            MouseArea {
                                id: aoRadiusHoverArea
                                anchors.fill: parent
                                acceptedButtons: Qt.NoButton
                                hoverEnabled: true
                            }
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 1
                            to: 10
                            stepSize: 0.1
                            visible: sidebar.rtSettingsVisible
                            value: sidebar.viewport ? sidebar.viewport.aoRadius : 3.0
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.aoRadius = value
                                }
                            }
                        }

                        Label {
                            id: ambientLabel
                            text: qsTr("Ambient: %1").arg(sidebar.viewport ? sidebar.viewport.ambientStrength.toFixed(2) : "0.30")
                            color: "#cccccc"
                            font.pixelSize: 11
                            visible: sidebar.rtSettingsVisible
                            ToolTip.visible: ambientHoverArea.containsMouse
                            ToolTip.text: qsTr("Base light intensity applied everywhere. Higher values brighten the whole scene, including shadowed areas.")

                            MouseArea {
                                id: ambientHoverArea
                                anchors.fill: parent
                                acceptedButtons: Qt.NoButton
                                hoverEnabled: true
                            }
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            stepSize: 0.01
                            visible: sidebar.rtSettingsVisible
                            value: sidebar.viewport ? sidebar.viewport.ambientStrength : 0.3
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.ambientStrength = value
                                }
                            }
                        }

                        Label {
                            id: diffuseLabel
                            text: qsTr("Diffuse: %1").arg(sidebar.viewport ? sidebar.viewport.diffuseStrength.toFixed(2) : "0.70")
                            color: "#cccccc"
                            font.pixelSize: 11
                            visible: sidebar.rtSettingsVisible
                            ToolTip.visible: diffuseHoverArea.containsMouse
                            ToolTip.text: qsTr("Strength of directional matte lighting. Higher values increase light-facing contrast and shape definition.")

                            MouseArea {
                                id: diffuseHoverArea
                                anchors.fill: parent
                                acceptedButtons: Qt.NoButton
                                hoverEnabled: true
                            }
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            stepSize: 0.01
                            visible: sidebar.rtSettingsVisible
                            value: sidebar.viewport ? sidebar.viewport.diffuseStrength : 0.7
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.diffuseStrength = value
                                }
                            }
                        }

                        Label {
                            id: specularLabel
                            text: qsTr("Specular: %1").arg(sidebar.viewport ? sidebar.viewport.specularStrength.toFixed(2) : "0.50")
                            color: "#cccccc"
                            font.pixelSize: 11
                            visible: sidebar.rtSettingsVisible
                            ToolTip.visible: specularHoverArea.containsMouse
                            ToolTip.text: qsTr("Brightness of reflective highlights. Higher values make highlights stronger and more noticeable.")

                            MouseArea {
                                id: specularHoverArea
                                anchors.fill: parent
                                acceptedButtons: Qt.NoButton
                                hoverEnabled: true
                            }
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            stepSize: 0.01
                            visible: sidebar.rtSettingsVisible
                            value: sidebar.viewport ? sidebar.viewport.specularStrength : 0.5
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.specularStrength = value
                                }
                            }
                        }

                        Label {
                            id: shininessLabel
                            text: qsTr("Shininess: %1").arg(sidebar.viewport ? Math.round(sidebar.viewport.shininess) : 32)
                            color: "#cccccc"
                            font.pixelSize: 11
                            visible: sidebar.rtSettingsVisible
                            ToolTip.visible: shininessHoverArea.containsMouse
                            ToolTip.text: qsTr("Sharpness of specular highlights. Higher values make highlights tighter; lower values make them softer.")

                            MouseArea {
                                id: shininessHoverArea
                                anchors.fill: parent
                                acceptedButtons: Qt.NoButton
                                hoverEnabled: true
                            }
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 1
                            to: 128
                            stepSize: 1
                            visible: sidebar.rtSettingsVisible
                            value: sidebar.viewport ? sidebar.viewport.shininess : 32
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.shininess = Math.round(value)
                                }
                            }
                        }

                        Label {
                            text: qsTr("Light direction (X): %1").arg(sidebar.viewport ? sidebar.viewport.lightDirX.toFixed(2) : "0.30")
                            color: "#cccccc"
                            font.pixelSize: 11
                            visible: sidebar.rtSettingsVisible
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: -1
                            to: 1
                            stepSize: 0.01
                            visible: sidebar.rtSettingsVisible
                            value: sidebar.viewport ? sidebar.viewport.lightDirX : 0.3
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.lightDirX = value
                                }
                            }
                        }

                        Label {
                            text: qsTr("Light direction (Y): %1").arg(sidebar.viewport ? sidebar.viewport.lightDirY.toFixed(2) : "0.80")
                            color: "#cccccc"
                            font.pixelSize: 11
                            visible: sidebar.rtSettingsVisible
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: -1
                            to: 1
                            stepSize: 0.01
                            visible: sidebar.rtSettingsVisible
                            value: sidebar.viewport ? sidebar.viewport.lightDirY : 0.8
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.lightDirY = value
                                }
                            }
                        }

                        Label {
                            text: qsTr("Light direction (Z): %1").arg(sidebar.viewport ? sidebar.viewport.lightDirZ.toFixed(2) : "0.50")
                            color: "#cccccc"
                            font.pixelSize: 11
                            visible: sidebar.rtSettingsVisible
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: -1
                            to: 1
                            stepSize: 0.01
                            visible: sidebar.rtSettingsVisible
                            value: sidebar.viewport ? sidebar.viewport.lightDirZ : 0.5
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.lightDirZ = value
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
                                backgroundRedSlider.value / 255.0,
                                backgroundGreenSlider.value / 255.0,
                                backgroundBlueSlider.value / 255.0,
                                1.0
                            )
                        }

                        Label {
                            text: qsTr("R")
                            color: "#ff7777"
                            font.pixelSize: 10
                        }

                        Slider {
                            id: backgroundRedSlider
                            Layout.fillWidth: true
                            from: 0
                            to: 255
                            stepSize: 1
                            value: sidebar.viewport ? Math.round(sidebar.viewport.backgroundColor.r * 255) : 230
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.backgroundColor = Qt.rgba(
                                        value / 255.0,
                                        backgroundGreenSlider.value / 255.0,
                                        backgroundBlueSlider.value / 255.0,
                                        1.0
                                    )
                                }
                            }
                        }

                        Label {
                            text: qsTr("G")
                            color: "#77ff77"
                            font.pixelSize: 10
                        }

                        Slider {
                            id: backgroundGreenSlider
                            Layout.fillWidth: true
                            from: 0
                            to: 255
                            stepSize: 1
                            value: sidebar.viewport ? Math.round(sidebar.viewport.backgroundColor.g * 255) : 230
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.backgroundColor = Qt.rgba(
                                        backgroundRedSlider.value / 255.0,
                                        value / 255.0,
                                        backgroundBlueSlider.value / 255.0,
                                        1.0
                                    )
                                }
                            }
                        }

                        Label {
                            text: qsTr("B")
                            color: "#7777ff"
                            font.pixelSize: 10
                        }

                        Slider {
                            id: backgroundBlueSlider
                            Layout.fillWidth: true
                            from: 0
                            to: 255
                            stepSize: 1
                            value: sidebar.viewport ? Math.round(sidebar.viewport.backgroundColor.b * 255) : 230
                            onMoved: {
                                if (sidebar.viewport) {
                                    sidebar.viewport.backgroundColor = Qt.rgba(
                                        backgroundRedSlider.value / 255.0,
                                        backgroundGreenSlider.value / 255.0,
                                        value / 255.0,
                                        1.0
                                    )
                                }
                            }
                        }

                        Button {
                            text: qsTr("Reset to Default")
                            Layout.fillWidth: true
                            onClicked: {
                                backgroundRedSlider.value = 230
                                backgroundGreenSlider.value = 230
                                backgroundBlueSlider.value = 230
                                if (sidebar.viewport) {
                                    sidebar.viewport.backgroundColor = Qt.rgba(
                                        230 / 255.0,
                                        230 / 255.0,
                                        230 / 255.0,
                                        1.0
                                    )
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
