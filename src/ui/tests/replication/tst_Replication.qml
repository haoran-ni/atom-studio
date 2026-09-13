import QtQuick
import QtTest
import AtomStudio 1.0
import "../../qml" as UI

Item {
    width: 360
    height: 800

    Component {
        id: sidebarComponent
        UI.Sidebar { width: 360; height: 800 }
    }

    TestCase {
        name: "Replication"
        when: windowShown
        property var sidebar
        property var section

        function init() {
            ReplicationFixture.load()
            sidebar = createTemporaryObject(sidebarComponent, parent)
            verify(sidebar !== null)
            section = findChild(sidebar, "structureManipulationSection")
            section.expanded = true
            tryCompare(section, "expansion", 1)
        }

        function control(axis, suffix) {
            const item = findChild(sidebar, "replication" + axis + suffix)
            verify(item !== null)
            return item
        }

        function typeValue(axis, text) {
            const input = control(axis, "Input")
            input.forceActiveFocus()
            input.selectAll()
            for (let i = 0; i < text.length; ++i)
                keyClick(text[i])
        }

        function test_arrowsGrowAndShrinkFromOriginal_data() {
            return [{tag: "X", axis: "X", index: 0, length: 3},
                    {tag: "Y", axis: "Y", index: 1, length: 4},
                    {tag: "Z", axis: "Z", index: 2, length: 5}]
        }

        function test_arrowsGrowAndShrinkFromOriginal(data) {
            verify(!control(data.axis, "Down").enabled)
            mouseClick(control(data.axis, "Up"))
            compare(control(data.axis, "Input").text, "2")
            compare(StructureModel.atomCount, 4)
            mouseClick(control(data.axis, "Up"))
            compare(StructureModel.atomCount, 6)
            compare(ReplicationFixture.latticeComponent(data.index, data.index), 3 * data.length)
            mouseClick(control(data.axis, "Down"))
            compare(StructureModel.atomCount, 4)
            mouseClick(control(data.axis, "Down"))
            compare(StructureModel.atomCount, 2)
            verify(!control(data.axis, "Down").enabled)
        }

        function test_typingRequiresEnter() {
            typeValue("X", "3")
            compare(StructureModel.atomCount, 2)
            control("Y", "Input").forceActiveFocus()
            compare(StructureModel.atomCount, 2)
            compare(control("X", "Input").text, "1")
            typeValue("X", "3")
            keyClick(Qt.Key_Return)
            compare(StructureModel.atomCount, 6)
            typeValue("Y", "2")
            keyClick(Qt.Key_Enter)
            compare(StructureModel.atomCount, 12)
            compare(ReplicationFixture.latticeComponent(0, 1), 1.5)
            compare(ReplicationFixture.latticeComponent(1, 2), 0.5)
        }

        function test_arrowUsesAppliedCounts() {
            typeValue("X", "7")
            mouseClick(control("Y", "Up"))
            compare(StructureModel.replicationX, 1)
            compare(StructureModel.replicationY, 2)
            compare(StructureModel.atomCount, 4)
            mouseClick(control("X", "Up"))
            compare(control("X", "Input").text, "2")
            compare(StructureModel.atomCount, 8)
        }

        function test_boundsAndInvalidInput() {
            typeValue("X", "99")
            keyClick(Qt.Key_Return)
            compare(StructureModel.atomCount, 198)
            verify(!control("X", "Up").enabled)
            typeValue("X", "0")
            keyClick(Qt.Key_Return)
            compare(StructureModel.replicationX, 99)
            compare(StructureModel.atomCount, 198)
            compare(control("X", "Input").text, "99")
            verify(sidebar.maxRTSamplesErrorMessage.length > 0)
        }

        function test_countsSurviveCollapseAndReset() {
            mouseClick(control("X", "Up"))
            section.expanded = false
            tryCompare(section, "expansion", 0)
            section.expanded = true
            tryCompare(section, "expansion", 1)
            compare(control("X", "Input").text, "2")
            mouseClick(control("Y", "Up"))
            StructureModel.resetToOriginal()
            compare(control("X", "Input").text, "2")
            compare(control("Y", "Input").text, "2")
            compare(StructureModel.atomCount, 8)
            mouseClick(control("Z", "Up"))
            ReplicationFixture.load()
            compare(control("Z", "Input").text, "1")
            compare(StructureModel.atomCount, 2)
            ReplicationFixture.load(false)
            verify(!control("X", "Input").enabled)
            verify(!control("X", "Up").enabled)
            StructureModel.clear()
            compare(StructureModel.replicationX, 1)
            verify(!control("X", "Input").enabled)
        }

        function test_globalReplicationAndNewImports() {
            ReplicationFixture.add()
            mouseClick(control("X", "Up"))
            compare(StructureModel.atomCount, 4)
            StructureModel.setActiveIndex(0)
            compare(StructureModel.atomCount, 4)
            compare(control("X", "Input").text, "2")
            ReplicationFixture.add()
            compare(StructureModel.atomCount, 4)
            ReplicationFixture.add(false)
            compare(StructureModel.atomCount, 2)
            verify(control("Y", "Up").enabled)
            mouseClick(control("Y", "Up"))
            compare(StructureModel.atomCount, 2)
            StructureModel.setActiveIndex(0)
            compare(StructureModel.atomCount, 8)
            compare(control("Y", "Input").text, "2")
        }

        function test_replicationDiscardsWorkingEdits() {
            mouseClick(control("X", "Up"))
            verify(ReplicationFixture.deleteFirstAtom())
            compare(StructureModel.atomCount, 3)
            mouseClick(control("Y", "Up"))
            compare(StructureModel.atomCount, 8)
            mouseClick(control("X", "Up"))
            compare(StructureModel.atomCount, 12)
        }
    }
}
