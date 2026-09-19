import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: control

    property string title: ""
    property string tooltipText: ""
    property var statusHintTarget: null
    property color titleColor: "#17181c"
    property int titlePixelSize: 13
    property bool integer: false
    property int decimals: integer ? 0 : 2
    property real defaultValue: 0
    property real sourceValue: defaultValue
    property int inputWidth: 64
    property real from: 0
    property real to: 1
    property real stepSize: 0
    property bool logarithmic: false

    readonly property real currentValue: normalizedValue(sliderValue(slider.value))

    signal valueApplied(real newValue)

    Layout.fillWidth: true
    spacing: 3
    opacity: enabled ? 1.0 : 0.45

    function usesLogarithmicScale() {
        return logarithmic && from > 0 && to > from
    }

    function boundedValue(rawValue) {
        return Math.max(from, Math.min(to, rawValue))
    }

    function normalizedValue(rawValue) {
        if (!Number.isFinite(rawValue)) {
            return defaultValue
        }
        return integer ? Math.round(rawValue) : rawValue
    }

    function sliderPosition(rawValue) {
        const value = boundedValue(normalizedValue(rawValue))
        if (!usesLogarithmicScale()) {
            return value
        }
        return (Math.log(value) - Math.log(from)) / (Math.log(to) - Math.log(from))
    }

    function sliderValue(position) {
        if (!usesLogarithmicScale()) {
            return position
        }
        return Math.exp(Math.log(from) + position * (Math.log(to) - Math.log(from)))
    }

    function valueFromUserSlider(position) {
        let value = normalizedValue(sliderValue(position))
        if (usesLogarithmicScale() && stepSize > 0) {
            value = Math.round((value - from) / stepSize) * stepSize + from
        }
        return boundedValue(value)
    }

    function showValue(rawValue) {
        slider.value = sliderPosition(rawValue)
        valueField.text = formatValue(currentValue)
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
                .arg(Math.round(control.from))
                .arg(Math.round(control.to))
        }

        return qsTr("Invalid value. Enter a number between %1 and %2.")
            .arg(Number(control.from).toFixed(decimals))
            .arg(Number(control.to).toFixed(decimals))
    }

    function revertText() {
        valueField.text = formatValue(currentValue)
    }

    function applyValue(newValue) {
        const normalized = normalizedValue(newValue)
        showValue(normalized)
        valueApplied(currentValue)
    }

    function updateStatusHint() {
        if (!statusHintTarget || tooltipText.length === 0) {
            return
        }

        if (titleHoverArea.containsMouse) {
            statusHintTarget.setStatusHint(control, tooltipText)
        } else {
            statusHintTarget.clearStatusHint(control)
        }
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

        if (parsed < control.from || parsed > control.to) {
            validationDialog.message = errorMessage()
            validationDialog.open()
            revertText()
            return
        }

        applyValue(parsed)
        // Pressing Enter otherwise leaves the field focused indefinitely.
        // While focused, source updates are intentionally ignored so an
        // in-progress edit is not overwritten. Release focus after a
        // successful commit so camera zoom changes can update the value again.
        valueField.focus = false
    }

    onSourceValueChanged: {
        if (!valueField.activeFocus) {
            showValue(sourceValue)
        }
    }

    Component.onCompleted: {
        showValue(sourceValue)
    }

    Component.onDestruction: {
        if (statusHintTarget && tooltipText.length > 0) {
            statusHintTarget.clearStatusHint(control)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Label {
            id: titleLabel
            text: control.title
            color: control.titleColor
            font.pixelSize: control.titlePixelSize
            font.weight: Font.DemiBold
            Layout.fillWidth: true
            elide: Text.ElideRight

            MouseArea {
                id: titleHoverArea
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                hoverEnabled: control.tooltipText.length > 0
                enabled: control.tooltipText.length > 0
                onContainsMouseChanged: control.updateStatusHint()
            }
        }

        TextField {
            id: valueField
            Layout.preferredWidth: control.inputWidth
            implicitHeight: 26
            horizontalAlignment: TextInput.AlignRight
            inputMethodHints: control.integer ? Qt.ImhDigitsOnly : Qt.ImhFormattedNumbersOnly
            selectByMouse: true
            color: "#17181c"
            font.pixelSize: 12
            leftPadding: 6
            rightPadding: 6
            topPadding: 3
            bottomPadding: 3
            onAccepted: control.commitText()

            background: Rectangle {
                radius: 7
                color: valueField.activeFocus ? "#f3f4f7" : "#ffffff"
                border.width: 1
                border.color: "#c7c9d1"
            }
        }

        Button {
            id: resetBtn
            implicitWidth: 26
            implicitHeight: 26

            Image {
                source: "qrc:/icons/reset.svg"
                width: 14
                height: 14
                sourceSize: Qt.size(14, 14)
                anchors.centerIn: parent
            }

            background: Rectangle {
                radius: 7
                color: resetBtn.down ? Qt.darker("#e2e3e8", 1.08) : "#e2e3e8"
                border.width: 1
                border.color: "#c7c9d1"
            }

            onClicked: control.applyValue(control.defaultValue)
        }
    }

    Slider {
        id: slider
        Layout.fillWidth: true
        implicitHeight: 22
        topPadding: 3
        bottomPadding: 3
        from: control.usesLogarithmicScale() ? 0 : control.from
        to: control.usesLogarithmicScale() ? 1 : control.to
        stepSize: control.usesLogarithmicScale() ? 0 : control.stepSize

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + (slider.availableHeight - height) / 2
            width: slider.availableWidth
            height: 4
            radius: 2
            color: "#e0e2e8"

            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: "#6e727d"
            }
        }

        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + (slider.availableHeight - height) / 2
            width: 16
            height: 16
            radius: 8
            color: slider.pressed ? "#f3f4f7" : "#ffffff"
            border.color: "#c7c9d1"
            border.width: 1
        }

        onValueChanged: control.revertText()
        onMoved: {
            const appliedValue = control.valueFromUserSlider(value)
            if (control.usesLogarithmicScale()) {
                slider.value = control.sliderPosition(appliedValue)
            }
            control.valueApplied(appliedValue)
        }
    }

    Dialog {
        id: validationDialog
        property string message: ""
        title: qsTr("Invalid Value")
        modal: true
        standardButtons: Dialog.Ok
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 340

        contentItem: Label {
            text: validationDialog.message
            color: "#33353c"
            wrapMode: Text.WordWrap
            padding: 12
        }
    }
}
