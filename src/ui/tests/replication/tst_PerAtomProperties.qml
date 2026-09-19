import QtQuick
import QtTest
import AtomStudio 1.0
import "../../qml" as UI

Item {
    width: 500; height: 800
    Component {
        id: component
        Item {
            width: 240; height: 650
            property alias editor: editor
            property alias picker: picker
            UI.ColorPickerPopup { id: picker }
            UI.PerAtomProperties { id: editor; width: parent.width; pickerPopup: picker }
        }
    }
    Component { id: sidebarComponent; UI.Sidebar { width: 400; height: 800 } }
    TestCase {
        name: "PerAtomProperties"
        when: windowShown

        function init() {
            StructureModel.switchingLocked = false
            StructureModel.atomRadiusType = 0
            ReplicationFixture.loadSpecies()
        }
        function setup() {
            const item = createTemporaryObject(component, parent)
            verify(item !== null)
            verify(findChild(item, "perAtomPropertiesTitle") !== null)
            compare(findChild(item, "atomPropertiesPanel").visible, true)
            tryVerify(function() { return findChild(item, "speciesRadius0") !== null })
            tryVerify(function() { return findChild(item, "atomPropertiesList").width > 200 && findChild(item, "atomPropertiesList").height > 150 })
            return item
        }
        function test_scrollAndEdit() {
            const item = setup()
            const list = findChild(item, "atomPropertiesList")
            compare(list.count, 40)
            verify(item.editor.idWidth < 50)
            verify(item.editor.speciesWidth < 100)
            verify(findChild(item, "atomPropertiesPanel").height <= 248)
            verify(list.contentHeight > list.height)
            verify(list.contentWidth > list.width)
            verify(findChild(item, "atomPropertiesVerticalScrollBar").size < 1)
            verify(findChild(item, "atomPropertiesHorizontalScrollBar").size < 1)
            const horizontal = findChild(item, "atomPropertiesHorizontalScrollBar")
            mouseDrag(horizontal, horizontal.width * horizontal.size / 2, horizontal.height / 2,
                      horizontal.width / 2, 0)
            verify(list.contentX > 0, "Horizontal scrollbar must move the columns")
            list.contentX = list.contentWidth - list.width
            const input = findChild(item, "speciesRadius0")
            input.forceActiveFocus()
            input.text = "1.25"
            keyClick(Qt.Key_Return)
            verify(Math.abs(ReplicationFixture.atomRadius(0) - 1.25) < 0.00001)
            compare(findChild(item, "speciesRadius2").text, "1.25")
            verify(Math.abs(ReplicationFixture.atomRadius(1) - .66) < 0.00001)
            input.text = "-1"
            keyClick(Qt.Key_Return)
            compare(input.invalid, true)
            verify(Math.abs(ReplicationFixture.atomRadius(0) - 1.25) < 0.00001)
            keyClick(Qt.Key_Escape)
            compare(input.text, "1.25")
            StructureModel.atomRadiusType = 1
            compare(input.text, "1.77")
            list.contentX = 0
            mouseClick(findChild(item, "speciesColor0"))
            tryCompare(item.picker, "opened", true)
            item.picker.chooseSwatch(Qt.rgba(1, 0, 0, 1))
            compare(ReplicationFixture.atomColor(0), "#ff0000")
            compare(ReplicationFixture.atomColor(2), "#ff0000")
            verify(ReplicationFixture.atomColor(1) !== "#ff0000") // Jmol oxygen is not pure red.
            list.contentY = 36
            tryCompare(item.picker, "visible", false)
            list.positionViewAtEnd()
            tryVerify(function() { return findChild(item, "speciesRadius38") !== null })
            compare(findChild(item, "speciesRadius38").text, "1.77")
            StructureModel.switchingLocked = true
            verify(!findChild(item, "speciesRadius38").enabled)
            StructureModel.switchingLocked = false
            item.width = 500
            tryCompare(horizontal, "visible", false)
        }
        function test_switchDiscardsDraftAndPicker() {
            const item = setup()
            const input = findChild(item, "speciesRadius0")
            input.forceActiveFocus()
            input.text = "2.5"
            ReplicationFixture.add(false)
            wait(0)
            verify(Math.abs(ReplicationFixture.atomRadius(0) - .76) < 0.00001)
            mouseClick(findChild(item, "speciesColor0"))
            tryCompare(item.picker, "opened", true)
            StructureModel.activeIndex = 0
            tryCompare(item.picker, "visible", false)
        }
        function test_sidebarPlacement() {
            const sidebar = createTemporaryObject(sidebarComponent, parent)
            const section = findChild(sidebar, "atomsSection")
            section.expanded = true
            tryCompare(section, "expansion", 1)
            verify(findChild(sidebar, "perAtomPropertiesTitle") !== null)
            verify(findChild(sidebar, "perAtomPropertiesToggle") === null)
            tryCompare(findChild(sidebar, "atomPropertiesPanel"), "visible", true)
            compare(findChild(sidebar, "atomPropertiesList").count, 40)
            section.expanded = false
            tryCompare(section, "expansion", 0)
            section.expanded = true
            tryCompare(section, "expansion", 1)
            tryCompare(findChild(sidebar, "atomPropertiesPanel"), "visible", true)
        }
    }
}
