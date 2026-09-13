pragma Singleton
import QtQml

QtObject {
    property int structureCount: 1
    property int activeIndex: 0
    property int activeId: 0
    property string fileName: "test.xyz"
    property bool switchingLocked: false
    function setActiveIndex(index) { activeIndex = index }
    readonly property bool hasStructure: true
}
