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

    property alias from: slider.from
    property alias to: slider.to
    property alias stepSize: slider.stepSize
    property alias currentValue: slider.value

    signal valueApplied(real newValue)

    Layout.fillWidth: true
    spacing: 3

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
                opacity: resetBtn.down ? 0.5 : 1.0
            }

            background: Rectangle {
                radius: 7
                color: resetBtn.down ? "#6e727d" : "#e2e3e8"
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
        width: 340

        contentItem: Label {
            text: validationDialog.message
            color: "#33353c"
            wrapMode: Text.WordWrap
            padding: 12
        }
    }
}
