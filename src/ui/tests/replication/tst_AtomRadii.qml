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
        name: "AtomRadii"
        when: windowShown

        function test_dropdownChangesRadiiAndSurvivesCollapse() {
            StructureModel.atomRadiusType = 0
            ReplicationFixture.load()
            const sidebar = createTemporaryObject(sidebarComponent, parent)
            verify(sidebar !== null)
            const section = findChild(sidebar, "atomsSection")
            section.expanded = true
            tryCompare(section, "expansion", 1)
            let combo = findChild(sidebar, "atomRadiiComboBox")
            verify(combo !== null)
            compare(combo.count, 2)
            compare(combo.textAt(0), "Covalent radii")
            compare(combo.textAt(1), "Alvarez vdw radii")
            compare(combo.currentIndex, 0)
            combo.forceActiveFocus()
            keyClick(Qt.Key_Down)
            compare(StructureModel.atomRadiusType, 1)
            verify(Math.abs(ReplicationFixture.atomRadius(0) - 1.77) < 0.00001)
            section.expanded = false
            tryCompare(section, "expansion", 0)
            section.expanded = true
            tryCompare(section, "expansion", 1)
            combo = findChild(sidebar, "atomRadiiComboBox")
            compare(combo.currentIndex, 1)
            StructureModel.switchingLocked = true
            verify(!combo.enabled)
            StructureModel.switchingLocked = false
            combo.forceActiveFocus()
            keyClick(Qt.Key_Up)
            compare(StructureModel.atomRadiusType, 0)
            verify(Math.abs(ReplicationFixture.atomRadius(0) - 0.76) < 0.00001)
        }
    }
}
