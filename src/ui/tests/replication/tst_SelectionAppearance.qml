import QtQuick
import QtTest
import AtomStudio 1.0
import "../../qml" as UI

Item {
    width: 500; height: 800
    Component {
        id: component
        Item {
            width: 400; height: 800
            property alias sidebar: sidebar
            property alias viewport: viewportStub
            QtObject {
                id: viewportStub
                property int rendererMode: 0
                property color backgroundColor: "white"
                property real bondRadius: 0.1
                property int atomColorScheme: 0
                property real atomScale: 1
                property real outlineWidth: 0.05
                property bool outlineEnabled: true
                property color outlineColor: "black"
            }
            UI.Sidebar { id: sidebar; anchors.fill: parent; viewport: viewportStub }
        }
    }
    TestCase {
        name: "SelectionAppearance"
        when: windowShown

        function init() {
            StructureModel.switchingLocked = false
            ReplicationFixture.loadSpecies(3)
        }
        function cleanup() {
            StructureModel.switchingLocked = false
        }
        function test_structureEditsReplaceMatchingOverrides() {
            const item = createTemporaryObject(component, parent)
            ReplicationFixture.selectBond()
            ReplicationFixture.selectAtom(0)
            verify(StructureModel.applyStrokeWidthToSelection(0.25))
            verify(StructureModel.applyStrokeColorToSelection("blue"))
            const atomColor = ReplicationFixture.atomColor(0)
            StructureModel.selectionMode = 0

            const section = findChild(item, "structureManipulationSection")
            section.expanded = true
            tryCompare(section, "expansion", 1)
            const width = findChild(section, "structureStrokeThickness")
            const color = findChild(section, "structureStrokeColor")
            verify(width && color && width.enabled && color.enabled)
            // Opening the section must not erase saved styles.
            fuzzyCompare(ReplicationFixture.atomStrokeWidth(0), 0.25, 0.00001)
            width.applyValue(0.08)
            compare(item.viewport.outlineWidth, 0.08)
            compare(ReplicationFixture.atomStrokeWidth(0), -1)
            compare(ReplicationFixture.bondStrokeWidth(), -1)
            compare(ReplicationFixture.atomStrokeColor(0), "#0000ff")

            ReplicationFixture.selectBond()
            ReplicationFixture.selectAtom(0)
            verify(StructureModel.applyStrokeWidthToSelection(0.3))
            mouseClick(findChild(color, "colorPickerButton"))
            const picker = color.pickerPopup
            tryCompare(picker, "opened", true)
            picker.chooseSwatch(Qt.rgba(0, 1, 0, 1))
            compare(item.viewport.outlineColor, "#00ff00")
            verify(StructureModel.selectedStroke.color === undefined)
            fuzzyCompare(ReplicationFixture.atomStrokeWidth(0), 0.3, 0.00001)
            fuzzyCompare(ReplicationFixture.bondStrokeWidth(), 0.3, 0.00001)
            compare(StructureModel.selectedAtomCount, 1)
            compare(StructureModel.selectedBondCount, 1)
            // Reapplying the current global values must still remove overrides.
            verify(StructureModel.applyStrokeColorToSelection("blue"))
            picker.chooseSwatch(Qt.rgba(0, 1, 0, 1))
            verify(StructureModel.selectedStroke.color === undefined)
            picker.close()
            width.applyValue(0.08)
            compare(ReplicationFixture.atomStrokeWidth(0), -1)
            compare(ReplicationFixture.bondStrokeWidth(), -1)
            compare(ReplicationFixture.atomColor(0), atomColor)
        }
        function test_controlsAndSelectionScope() {
            const item = createTemporaryObject(component, parent)
            const section = findChild(item, "selectionSection")
            section.expanded = true
            tryCompare(section, "expansion", 1)
            const atomColor = findChild(section, "selectionAtomColor")
            const strokeColor = findChild(section, "selectionStrokeColor")
            const thickness = findChild(section, "selectionStrokeThickness")
            verify(atomColor && strokeColor && thickness)
            compare(atomColor.enabled, false)
            compare(strokeColor.enabled, false)
            compare(thickness.enabled, false)

            ReplicationFixture.selectAtom(0)
            compare(atomColor.enabled, true)
            compare(strokeColor.enabled, true)
            compare(thickness.enabled, true)
            compare(thickness.currentValue, 0.05)
            mouseClick(findChild(atomColor, "colorPickerButton"))
            const picker = atomColor.pickerPopup
            tryCompare(picker, "opened", true)
            picker.chooseSwatch(Qt.rgba(0, 0, 1, 1))
            compare(ReplicationFixture.atomColor(0), "#0000ff")
            verify(ReplicationFixture.atomColor(2) !== "#0000ff")
            picker.close()
            tryCompare(picker, "visible", false)

            thickness.applyValue(0.22)
            fuzzyCompare(ReplicationFixture.atomStrokeWidth(0), 0.22, 0.00001)
            compare(ReplicationFixture.atomStrokeWidth(2), -1)
            compare(item.viewport.outlineWidth, 0.05)
            mouseClick(findChild(strokeColor, "colorPickerButton"))
            tryCompare(picker, "opened", true)
            picker.chooseSwatch(Qt.rgba(0, 1, 0, 1))
            compare(ReplicationFixture.atomStrokeColor(0), "#00ff00")
            compare(item.viewport.outlineColor, "#000000")
            StructureModel.clearSelection()
            tryCompare(picker, "visible", false)
            compare(atomColor.enabled, false)
            compare(strokeColor.enabled, false)
            compare(thickness.enabled, false)

            // Deselect/reselect must reveal the saved object style, without editing it.
            ReplicationFixture.selectAtom(0)
            fuzzyCompare(thickness.currentValue, 0.22, 0.00001)
            compare(strokeColor.sourceColor, "#00ff00")
            StructureModel.selectionMode = 0
            fuzzyCompare(ReplicationFixture.atomStrokeWidth(0), 0.22, 0.00001)
            compare(ReplicationFixture.atomStrokeColor(0), "#00ff00")

            ReplicationFixture.selectBond()
            compare(atomColor.enabled, false)
            compare(strokeColor.enabled, true)
            compare(thickness.enabled, true)
            compare(thickness.currentValue, 0.05)
            compare(strokeColor.sourceColor, "#000000")
            thickness.applyValue(0.3)
            fuzzyCompare(ReplicationFixture.bondStrokeWidth(), 0.3, 0.00001)
            fuzzyCompare(ReplicationFixture.atomStrokeWidth(0), 0.22, 0.00001)
            StructureModel.switchingLocked = true
            compare(strokeColor.enabled, false)
            compare(thickness.enabled, false)
            StructureModel.switchingLocked = false
            fuzzyCompare(thickness.currentValue, 0.3, 0.00001)
            compare(strokeColor.sourceColor, "#000000")
            StructureModel.selectionMode = 0
            compare(strokeColor.enabled, false)

            const atoms = findChild(item, "atomsSection")
            atoms.expanded = true
            tryCompare(atoms, "expansion", 1)
            verify(findChild(atoms, "selectionAtomColor") === null)
            verify(findChild(atoms, "perAtomPropertiesTitle") !== null)
        }
    }
}
