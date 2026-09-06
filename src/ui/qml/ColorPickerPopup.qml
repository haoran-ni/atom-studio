pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ColorUtils.js" as ColorUtils

Popup {
    id: root

    property var owner: null
    property color currentColor: "black"
    property real hue: 0
    property real saturation: 1
    property real lightness: 0
    property int currentTab: 0
    property var customColors: []
    readonly property bool showAlpha: owner ? owner.showAlpha : false
    property bool applying: false
    property bool pending: false
    readonly property var presetColors: ["#000000", "#ffffff", "#0066ff", "#2bc453", "#ffcc00", "#ff3b30"]
    readonly property Item windowOverlay: Overlay.overlay

    // The anchor is the logical parent; Qt renders the popup in the overlay.
    // Outside-parent dismissal lets the color button toggle an open picker.
    parent: owner ? owner.anchorItem : windowOverlay
    width: Math.min(340, windowOverlay ? windowOverlay.width - 24 : 340)
    height: Math.min(contentColumn.implicitHeight + topPadding + bottomPadding,
                     windowOverlay ? windowOverlay.height - 24 : 620)
    padding: 16
    margins: 12
    focus: true
    modal: false
    dim: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    function setColor(color, preserveHsl) {
        currentColor = Qt.rgba(color.r, color.g, color.b, showAlpha ? color.a : 1)
        if (!preserveHsl) {
            lightness = currentColor.hslLightness
            // Hue is undefined for grays, and saturation at black/white. Keep
            // these coordinates so moving back into the spectrum feels stable.
            if (currentColor.hslSaturation > 0 && currentColor.hslHue >= 0)
                hue = currentColor.hslHue
            if (lightness > 0.00001 && lightness < 0.99999)
                saturation = currentColor.hslSaturation
        }
    }

    function syncFrom(source) {
        if (owner !== source || applying)
            return
        discardPending()
        setColor(source.sourceColor, false)
    }

    function discardPending() {
        updateTimer.stop()
        pending = false
    }

    function release(source) {
        if (owner !== source)
            return
        discardPending()
        owner = null
        close()
    }

    function reposition() {
        if (!owner || !windowOverlay)
            return
        const anchor = owner.anchorItem
        const point = anchor.mapToItem(windowOverlay, 0, 0)
        x = ColorUtils.clamp(point.x + anchor.width - width, 12, windowOverlay.width - width - 12) - point.x
        const below = point.y + anchor.height + 8
        const above = point.y - height - 8
        y = ColorUtils.clamp(below + height <= windowOverlay.height - 12 ? below : above,
                            12, windowOverlay.height - height - 12) - point.y
    }

    function toggleFor(source) {
        if (visible && owner === source) {
            close()
            return
        }
        contentColumn.forceActiveFocus()
        flush()
        owner = source
        hue = 0
        saturation = 1
        setColor(source.sourceColor, false)
        reposition()
        open()
    }

    function editColor(color, continuous, preserveHsl) {
        if (!owner || !owner.enabled)
            return
        setColor(color, preserveHsl === true)
        pending = true
        if (continuous) {
            if (!updateTimer.running)
                updateTimer.start()
        } else {
            flush()
        }
    }

    function flush() {
        updateTimer.stop()
        if (!pending)
            return
        pending = false
        if (!owner || !owner.enabled || !owner.visible)
            return
        applying = true
        owner.colorApplied(currentColor)
        applying = false
    }

    function editChannel(channel, value) {
        editColor(Qt.rgba(channel === 0 ? value / 255 : currentColor.r,
                          channel === 1 ? value / 255 : currentColor.g,
                          channel === 2 ? value / 255 : currentColor.b,
                          currentColor.a), true)
    }

    function editSpectrum(newHue, newLightness) {
        hue = newHue
        lightness = newLightness
        editColor(Qt.hsla(hue, saturation, lightness, currentColor.a), true, true)
    }

    function chooseSwatch(color) {
        editColor(Qt.rgba(color.r, color.g, color.b, currentColor.a), false)
    }

    function saveSwatch() {
        const value = currentColor.toString()
        const colors = customColors.filter(function(color) { return color !== value })
        colors.push(value)
        // Keep the shared, session-local palette compact.
        customColors = colors.slice(-12)
    }

    function commitHex() {
        const parsed = ColorUtils.fromHex(hexField.text, currentColor.a)
        if (parsed !== null)
            editColor(parsed, false)
        hexField.text = ColorUtils.hex(currentColor)
        hexField.invalid = parsed === null
    }

    onAboutToHide: {
        contentColumn.forceActiveFocus()
        flush()
        const previousOwner = owner
        owner = null
        if (previousOwner && previousOwner.visible && previousOwner.enabled)
            previousOwner.anchorItem.forceActiveFocus()
    }
    onHeightChanged: {
        if (visible)
            reposition()
    }
    onWidthChanged: {
        if (visible)
            reposition()
    }
    onCurrentColorChanged: {
        if (!hexField.activeFocus)
            hexField.text = ColorUtils.hex(currentColor)
    }

    Connections {
        target: root.windowOverlay
        function onWidthChanged() { root.reposition() }
        function onHeightChanged() { root.reposition() }
    }

    Timer {
        id: updateTimer
        interval: 16
        onTriggered: root.flush()
    }

    background: Rectangle {
        color: "#f7f7f8"
        radius: 12
        border.color: "#d1d2d6"
        border.width: 1
    }

    component IconButton: Button {
        id: control
        property url iconSource
        implicitWidth: 28
        implicitHeight: 28
        padding: 6
        contentItem: Image {
            source: control.iconSource
            sourceSize: Qt.size(16, 16)
        }
        background: Rectangle {
            radius: width / 2
            color: control.down ? "#d0d1d5" : control.hovered ? "#dedfe3" : "#e9e9ec"
            border.width: control.activeFocus ? 2 : 0
            border.color: "#246bdb"
        }
        ToolTip.visible: hovered
        ToolTip.text: Accessible.name
    }

    component SwatchButton: Button {
        id: control
        property color swatchColor: "black"
        property bool restoreAlpha: false
        property bool selected: ColorUtils.sameRgb(root.currentColor, swatchColor)
                                && (!restoreAlpha || !root.showAlpha
                                    || Math.abs(root.currentColor.a - swatchColor.a) < 0.002)
        implicitWidth: 27
        implicitHeight: 27
        padding: 3
        Accessible.name: qsTr("Color #%1").arg(ColorUtils.hex(swatchColor))
        onClicked: {
            if (restoreAlpha)
                root.editColor(swatchColor, false)
            else
                root.chooseSwatch(swatchColor)
        }
        background: Rectangle {
            radius: width / 2
            color: "transparent"
            border.width: control.selected || control.activeFocus ? 2 : 0
            border.color: control.activeFocus ? "#246bdb" : "#17181c"
        }
        contentItem: ColorSwatch {
            color: control.swatchColor
            radius: width / 2
            borderColor: "#22000000"
        }
        ToolTip.visible: hovered
        ToolTip.text: Accessible.name
    }

    contentItem: ScrollView {
        id: scrollView
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: contentColumn
            width: scrollView.availableWidth
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Colors")
                    horizontalAlignment: Text.AlignHCenter
                    color: "#17181c"
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                IconButton {
                    objectName: "closeColorPicker"
                    iconSource: "qrc:/icons/close.svg"
                    Accessible.name: qsTr("Close color picker")
                    onClicked: root.close()
                }
            }

            TabBar {
                id: tabs
                Layout.fillWidth: true
                implicitHeight: 30
                padding: 2
                spacing: 2
                currentIndex: root.currentTab
                onCurrentIndexChanged: root.currentTab = currentIndex
                background: Rectangle { color: "#e8e8eb"; radius: 7 }

                Repeater {
                    model: [qsTr("Grid"), qsTr("Spectrum"), qsTr("Sliders")]
                    TabButton {
                        id: tabButton
                        required property string modelData
                        objectName: modelData + "Tab"
                        text: modelData
                        implicitHeight: 26
                        padding: 3
                        contentItem: Text {
                            text: tabButton.text
                            font.pixelSize: 12
                            font.weight: tabButton.checked ? Font.DemiBold : Font.Normal
                            color: "#17181c"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 5
                            color: tabButton.checked ? "white" : "transparent"
                            border.width: tabButton.activeFocus ? 1 : 0
                            border.color: "#246bdb"
                        }
                    }
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 252
                currentIndex: tabs.currentIndex

                Grid {
                    id: grid
                    property int keyboardIndex: 0
                    columns: 12
                    rows: 10
                    spacing: 0

                    Repeater {
                        id: gridRepeater
                        model: 120
                        Button {
                            id: cell
                            required property int index
                            readonly property color swatchColor: ColorUtils.gridColor(index)
                            readonly property bool selected: ColorUtils.sameRgb(root.currentColor, swatchColor)
                            objectName: "gridColor" + index
                            width: grid.width / 12
                            height: grid.height / 10
                            padding: 0
                            focusPolicy: index === grid.keyboardIndex ? Qt.StrongFocus : Qt.ClickFocus
                            onActiveFocusChanged: {
                                if (activeFocus)
                                    grid.keyboardIndex = index
                            }
                            Accessible.name: qsTr("Color #%1").arg(ColorUtils.hex(swatchColor))
                            onClicked: root.chooseSwatch(swatchColor)
                            background: Rectangle {
                                color: cell.swatchColor
                                border.color: "white"
                                border.width: cell.selected || cell.activeFocus ? 2 : 0
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 2
                                    color: "transparent"
                                    border.width: cell.selected || cell.activeFocus ? 1 : 0
                                    border.color: cell.activeFocus ? "#246bdb" : "#17181c"
                                }
                            }
                            Keys.onPressed: function(event) {
                                let next = index
                                if (event.key === Qt.Key_Left) next -= 1
                                else if (event.key === Qt.Key_Right) next += 1
                                else if (event.key === Qt.Key_Up) next -= 12
                                else if (event.key === Qt.Key_Down) next += 12
                                else return
                                next = ColorUtils.clamp(next, 0, 119)
                                gridRepeater.itemAt(next).forceActiveFocus()
                                event.accepted = true
                            }
                        }
                    }
                }

                ColumnLayout {
                    spacing: 12
                    ColorSpectrum {
                        objectName: "colorSpectrum"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: 10
                        hue: root.hue
                        saturation: root.saturation
                        lightness: root.lightness
                        selectedColor: Qt.rgba(root.currentColor.r, root.currentColor.g, root.currentColor.b, 1)
                        onColorEdited: function(hue, lightness) { root.editSpectrum(hue, lightness) }
                        onEditFinished: root.flush()
                    }
                    ColorChannelSlider {
                        objectName: "saturation"
                        title: qsTr("SATURATION")
                        maximum: 100
                        suffix: "%"
                        value: Math.round(root.saturation * 100)
                        startColor: Qt.hsla(root.hue, 0, 0.5, 1)
                        endColor: Qt.hsla(root.hue, 1, 0.5, 1)
                        onValueEdited: function(value) {
                            root.saturation = value / 100
                            root.editSpectrum(root.hue, root.lightness)
                        }
                        onEditFinished: root.flush()
                    }
                }

                ColumnLayout {
                    spacing: 16
                    Repeater {
                        model: [qsTr("RED"), qsTr("GREEN"), qsTr("BLUE")]
                        ColorChannelSlider {
                            required property int index
                            required property string modelData
                            objectName: ["red", "green", "blue"][index]
                            title: modelData
                            value: Math.round([root.currentColor.r, root.currentColor.g, root.currentColor.b][index] * 255)
                            startColor: Qt.rgba(index === 0 ? 0 : root.currentColor.r,
                                                index === 1 ? 0 : root.currentColor.g,
                                                index === 2 ? 0 : root.currentColor.b, 1)
                            endColor: Qt.rgba(index === 0 ? 1 : root.currentColor.r,
                                              index === 1 ? 1 : root.currentColor.g,
                                              index === 2 ? 1 : root.currentColor.b, 1)
                            onValueEdited: function(value) { root.editChannel(index, value) }
                            onEditFinished: root.flush()
                        }
                    }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Hex Color #")
                            horizontalAlignment: Text.AlignRight
                            color: "#777980"
                            font.pixelSize: 12
                        }
                        TextField {
                            id: hexField
                            objectName: "hexColorField"
                            property bool invalid: false
                            Layout.preferredWidth: 100
                            implicitHeight: 30
                            text: ColorUtils.hex(root.currentColor)
                            horizontalAlignment: TextInput.AlignHCenter
                            selectByMouse: true
                            font.pixelSize: 13
                            color: "#17181c"
                            Accessible.name: qsTr("Hex color")
                            onTextEdited: invalid = false
                            onEditingFinished: {
                                if (text !== ColorUtils.hex(root.currentColor))
                                    root.commitHex()
                            }
                            onAccepted: focus = false
                            background: Rectangle {
                                radius: 6
                                color: "white"
                                border.width: 1
                                border.color: hexField.invalid ? "#cf4242"
                                              : hexField.activeFocus ? "#246bdb" : "#e5e5e8"
                            }
                            ToolTip.visible: invalid && hovered
                            ToolTip.text: qsTr("Enter six hexadecimal digits, for example FF8800.")
                        }
                    }
                }
            }

            ColorChannelSlider {
                objectName: "alpha"
                visible: root.showAlpha
                title: qsTr("OPACITY")
                maximum: 100
                suffix: "%"
                value: Math.round(root.currentColor.a * 100)
                startColor: Qt.rgba(root.currentColor.r, root.currentColor.g, root.currentColor.b, 0)
                endColor: Qt.rgba(root.currentColor.r, root.currentColor.g, root.currentColor.b, 1)
                onValueEdited: function(value) {
                    root.editColor(Qt.rgba(root.currentColor.r, root.currentColor.g, root.currentColor.b, value / 100), true, true)
                }
                onEditFinished: root.flush()
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#dedee2" }

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 16
                ColorSwatch {
                    Layout.preferredWidth: 52
                    Layout.preferredHeight: 52
                    Layout.alignment: Qt.AlignTop
                    color: root.currentColor
                    radius: 8
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Flow {
                        Layout.fillWidth: true
                        spacing: 7
                        Repeater {
                            model: root.presetColors
                            SwatchButton {
                                required property string modelData
                                swatchColor: modelData
                            }
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 7
                        Repeater {
                            model: root.customColors
                            SwatchButton {
                                required property string modelData
                                swatchColor: modelData
                                restoreAlpha: true
                            }
                        }
                        IconButton {
                            objectName: "saveColorSwatch"
                            iconSource: "qrc:/icons/plus.svg"
                            Accessible.name: qsTr("Save current color")
                            onClicked: root.saveSwatch()
                        }
                        IconButton {
                            objectName: "resetColor"
                            iconSource: "qrc:/icons/reset.svg"
                            Accessible.name: qsTr("Reset color to default")
                            onClicked: {
                                if (root.owner)
                                    root.editColor(root.owner.defaultColor, false)
                            }
                        }
                    }
                }
            }
        }
    }
}
