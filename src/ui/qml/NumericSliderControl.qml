import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
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
            validationDialog.message = errorMessage()
            validationDialog.open()
            revertText()
            return
        }

        let parsed = NaN
        if (integer) {
            if (!/^[+-]?\d+$/.test(rawText)) {
                validationDialog.message = errorMessage()
                validationDialog.open()
                revertText()
                return
            }
            parsed = Number(rawText)
            if (!Number.isInteger(parsed)) {
                validationDialog.message = errorMessage()
                validationDialog.open()
                revertText()
                return
            }
        } else {
            parsed = Number(rawText)
            if (!Number.isFinite(parsed)) {
                validationDialog.message = errorMessage()
                validationDialog.open()
                revertText()
                return
            }
        }

        if (parsed < slider.from || parsed > slider.to) {
            validationDialog.message = errorMessage()
            validationDialog.open()
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

    Dialog {
        id: validationDialog
        property string message: ""
        title: qsTr("Invalid Value")
        modal: true
        standardButtons: Dialog.Ok
        parent: Overlay.overlay
        anchors.centerIn: parent

        contentItem: Label {
            text: validationDialog.message
            color: "#cccccc"
            wrapMode: Text.WordWrap
            padding: 12
            width: 316
        }
    }
}
