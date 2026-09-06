import QtQuick
import QtQuick.Controls
import QtTest
import "../qml" as UI
import "../qml/ColorUtils.js" as Colors

Item {
    id: window
    width: 800
    height: 700
    visible: true

    UI.ColorPickerPopup { id: popup }

    UI.ColorPicker {
        id: background
        x: 610
        y: 500
        width: 170
        title: "Background Color"
        pickerPopup: popup
        showAlpha: true
        defaultColor: "white"
        onColorApplied: function(color) { sourceColor = color }
    }

    UI.ColorPicker {
        id: atoms
        x: 610
        y: 100
        width: 170
        pickerPopup: popup
        onColorApplied: function(color) { sourceColor = color }
    }

    SignalSpy { id: applied; target: background; signalName: "colorApplied" }

    Component {
        id: temporaryPicker
        UI.ColorPicker {
            x: 20
            y: 100
            width: 170
            pickerPopup: popup
        }
    }

    TestCase {
        name: "ColorPicker"
        when: windowShown

        function init() {
            popup.release(popup.owner)
            background.enabled = true
            background.visible = true
            background.sourceColor = Qt.rgba(0.2, 0.4, 0.6, 0.4)
            atoms.sourceColor = "black"
            popup.currentTab = 0
            popup.customColors = []
            applied.clear()
        }

        function cleanup() {
            popup.release(popup.owner)
        }

        function openBackground(tab) {
            mouseClick(background.anchorItem)
            tryCompare(popup, "opened", true)
            popup.currentTab = tab === undefined ? 0 : tab
            wait(0)
        }

        function field(name) {
            const result = findChild(popup.contentItem, name)
            verify(result !== null, name)
            return result
        }

        function enter(name, text) {
            const input = field(name)
            input.forceActiveFocus()
            input.text = text
            keyClick(Qt.Key_Return)
        }

        function test_tabsDoNotModifyColor() {
            openBackground()
            for (let i = 0; i < 3; ++i) {
                mouseClick(field(["GridTab", "SpectrumTab", "SlidersTab"][i]))
                compare(popup.currentTab, i)
                compare(Colors.hex(popup.currentColor), "336699")
            }
            compare(applied.count, 0)
            popup.close()
            tryCompare(popup, "visible", false)
            compare(background.anchorItem.activeFocus, true)
        }

        function test_gridPreservesAlphaAndResetWorks() {
            openBackground()
            mouseClick(field("gridColor0"))
            compare(Colors.hex(background.sourceColor), "FFFFFF")
            fuzzyCompare(background.sourceColor.a, 0.4, 0.001)
            mouseClick(field("resetColor"))
            compare(background.sourceColor, background.defaultColor)
        }

        function test_rgbAndHexValidation() {
            openBackground(2)
            enter("redField", "128")
            compare(Math.round(background.sourceColor.r * 255), 128)
            enter("redField", "999")
            compare(Math.round(background.sourceColor.r * 255), 128)
            compare(field("redField").invalid, true)
            enter("greenField", "-1")
            compare(Math.round(background.sourceColor.g * 255), 102)
            enter("hexColorField", "#aB12fF")
            compare(Colors.hex(background.sourceColor), "AB12FF")
            fuzzyCompare(background.sourceColor.a, 0.4, 0.001)
            enter("hexColorField", "oops")
            compare(Colors.hex(background.sourceColor), "AB12FF")
            compare(field("hexColorField").invalid, true)
            background.sourceColor = "#123456"
            compare(field("redField").text, "18")
            compare(field("hexColorField").text, "123456")
        }

        function test_opacityAndOpaqueOwner() {
            openBackground(2)
            enter("alphaField", "25%")
            fuzzyCompare(background.sourceColor.a, 0.25, 0.001)
            popup.close()
            popup.toggleFor(atoms)
            compare(popup.showAlpha, false)
            popup.editColor(Qt.rgba(1, 0, 0, 0.25), false)
            compare(atoms.sourceColor.a, 1)
        }

        function test_spectrumAndHueAtBlack() {
            openBackground(1)
            popup.editColor(Qt.hsla(0.6, 0.7, 0.5, 0.4), false)
            popup.editSpectrum(0.6, 0)
            popup.flush()
            compare(Colors.hex(background.sourceColor), "000000")
            fuzzyCompare(popup.hue, 0.6, 0.001)
            fuzzyCompare(popup.saturation, 0.7, 0.001)
            popup.editSpectrum(popup.hue, 0.5)
            popup.flush()
            fuzzyCompare(background.sourceColor.hslHue, 0.6, 0.001)
            const spectrum = field("colorSpectrum")
            mouseClick(spectrum, spectrum.width / 2, spectrum.height / 3)
            fuzzyCompare(background.sourceColor.hslHue, 1/3, 0.01)
            fuzzyCompare(background.sourceColor.hslLightness, 0.5, 0.01)
            spectrum.forceActiveFocus()
            keyClick(Qt.Key_Right)
            verify(background.sourceColor.hslLightness < 0.5)
        }

        function test_dragCoalescingAndFinalValue() {
            openBackground(2)
            popup.editColor(Qt.rgba(1, 0, 0, 1), true)
            popup.editColor(Qt.rgba(0, 1, 0, 1), true)
            popup.editColor(Qt.rgba(0, 0, 1, 1), true)
            compare(applied.count, 0)
            tryCompare(applied, "count", 1)
            compare(Colors.hex(background.sourceColor), "0000FF")
            popup.editColor(Qt.rgba(1, 1, 0, 1), true)
            popup.close()
            compare(Colors.hex(background.sourceColor), "FFFF00")
            wait(30)
            compare(applied.count, 2)
        }

        function test_externalResetWinsOverPendingDrag() {
            openBackground()
            popup.editColor(Qt.rgba(1, 0, 0, 1), true)
            background.sourceColor = "white"
            wait(30)
            compare(Colors.hex(popup.currentColor), "FFFFFF")
            compare(applied.count, 0)
        }

        function test_releasedOwnerNeverReceivesPendingEdit() {
            openBackground()
            popup.editColor(Qt.rgba(1, 0, 0, 1), true)
            popup.release(background)
            popup.toggleFor(atoms)
            wait(30)
            compare(Colors.hex(background.sourceColor), "336699")
            compare(Colors.hex(atoms.sourceColor), "000000")
            compare(applied.count, 0)
        }

        function test_disabledOwnerClosesPopup() {
            openBackground()
            popup.editColor(Qt.rgba(1, 0, 0, 1), true)
            background.enabled = false
            tryCompare(popup, "visible", false)
            wait(30)
            compare(applied.count, 0)
        }

        function test_savedSwatchesAndTabSurviveReopen() {
            openBackground(1)
            mouseClick(field("saveColorSwatch"))
            compare(popup.customColors.length, 1)
            popup.close()
            popup.toggleFor(atoms)
            compare(popup.currentTab, 1)
            compare(popup.customColors.length, 1)
        }

        function test_buttonTogglesOpenPopup() {
            mouseClick(atoms.anchorItem)
            tryCompare(popup, "opened", true)
            mouseClick(atoms.anchorItem)
            tryCompare(popup, "visible", false)
        }

        function test_switchingOwnerCommitsTextToOriginalOwner() {
            openBackground(2)
            const hex = field("hexColorField")
            hex.forceActiveFocus()
            hex.text = "FF0088"
            popup.toggleFor(atoms)
            compare(Colors.hex(background.sourceColor), "FF0088")
            compare(Colors.hex(atoms.sourceColor), "000000")
            compare(hex.text, "000000")
        }

        function test_destroyingOwnerClosesPopup() {
            const picker = temporaryPicker.createObject(window)
            verify(picker !== null)
            popup.toggleFor(picker)
            popup.editColor(Qt.rgba(1, 0, 0, 1), true)
            picker.destroy()
            tryCompare(popup, "visible", false)
            compare(popup.owner, null)
            compare(popup.pending, false)
        }

        function test_escapeAndOutsideClickDismiss() {
            openBackground()
            keyClick(Qt.Key_Escape)
            tryCompare(popup, "visible", false)
            openBackground()
            mouseClick(window, 20, 20)
            tryCompare(popup, "visible", false)
        }

        function test_popupStaysInsideResizedWindow() {
            openBackground()
            window.Window.window.width = 800
            window.Window.window.height = 600
            wait(0)
            const point = popup.parent.mapToItem(popup.windowOverlay, popup.x, popup.y)
            verify(point.x >= 12)
            verify(point.y >= 12)
            verify(point.x + popup.width <= popup.windowOverlay.width - 12)
            verify(point.y + popup.height <= popup.windowOverlay.height - 12)
            window.Window.window.height = 700
        }
    }
}
