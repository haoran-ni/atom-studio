import QtQuick
import QtTest
import AtomStudio 1.0
import "../../qml" as UI

Item {
    width: 480
    height: 360

    Component {
        id: panelComponent
        UI.ViewportPanel { width: 480; height: 360 }
    }

    TestCase {
        name: "ImageExport"
        when: windowShown

        function init() {
            ExportFiles.clear()
            StructureModel.structureCount = 3
            StructureModel.activeIndex = 0
            FileController.loadedUrls = []
        }

        function test_dropAllFiles() {
            var panel = createTemporaryObject(panelComponent, parent)
            verify(waitForRendering(panel))
            verify(ExportFiles.dropFiles(panel))
            compare(FileController.loadedUrls.length, 2)
            compare(FileController.loadedUrls[0].toString(), "file:///tmp/first.xyz")
            compare(FileController.loadedUrls[1].toString(), "file:///tmp/second.xyz")
        }

        function test_switcher() {
            var panel = createTemporaryObject(panelComponent, parent)
            var switcher = findChild(panel, "structureSwitcher")
            verify(switcher.visible)
            verify(waitForRendering(panel))
            var slider = findChild(panel, "structureSlider")
            mouseClick(slider, slider.width - 1, slider.height / 2)
            compare(StructureModel.activeIndex, 2)
            var previous = findChild(panel, "previousStructure")
            mouseClick(previous)
            compare(StructureModel.activeIndex, 1)
            slider.forceActiveFocus()
            keyClick(Qt.Key_Left)
            compare(StructureModel.activeIndex, 0)
            StructureModel.structureCount = 1
            verify(!switcher.visible)
        }

        function test_waitsForRequestedFrame_data() {
            return [
                { tag: "first-export-without-axes", axes: false, transparent: false, initialAlpha: 1 },
                { tag: "transparent-without-axes", axes: false, transparent: true, initialAlpha: 1 },
                { tag: "transparent-with-axes", axes: true, transparent: true, initialAlpha: 1 },
                { tag: "unchanged-viewport", axes: true, transparent: false, initialAlpha: 1 },
                { tag: "already-transparent", axes: true, transparent: true, initialAlpha: 0 }
            ]
        }

        function test_waitsForRequestedFrame(data) {
            var panel = createTemporaryObject(panelComponent, parent)
            verify(panel !== null)
            var viewport = panel.viewport
            var switcher = findChild(panel, "structureSwitcher")
            switcher.color = "magenta"
            verify(switcher.visible)
            viewport.backgroundColor = Qt.rgba(1, 1, 1, data.initialAlpha)
            FileController.saveImagePathSelected(ExportFiles.outputPath, ".png", data.axes, data.transparent)

            verify(!switcher.visible)
            verify(StructureModel.switchingLocked)
            viewport.presentOldFrame()
            wait(100) // Allow any incorrectly started grabToImage callback to finish.
            verify(panel.imageExportInProgress, "An old frame must not finish the export")
            verify(!ExportFiles.exists(), "An old frame must not be saved")

            viewport.presentRequestedFrame()
            tryCompare(panel, "imageExportInProgress", false)
            verify(ExportFiles.exists())
            verify(switcher.visible)
            verify(!StructureModel.switchingLocked)
            var sliderPixel = ExportFiles.pixel(240, 315)
            compare(sliderPixel.r, sliderPixel.g, "Switcher must be absent from exports")
            // Qt's software Canvas capture flattens alpha when the QML axes
            // labels are visible. Native GPU checks cover that combination.
            if (!data.axes) {
                compare(ExportFiles.pixel(2, 2).a, data.transparent ? 0 : data.initialAlpha)
            }
            var axesPixel = ExportFiles.pixel(30, 30)
            if (data.axes) {
                compare(axesPixel, Qt.rgba(1, 0, 0, 1))
            } else {
                compare(axesPixel.a, data.transparent ? 0 : data.initialAlpha)
                compare(axesPixel.r, axesPixel.g, "The exported frame must not contain the red axes")
            }
            compare(viewport.backgroundColor, Qt.rgba(1, 1, 1, data.initialAlpha))
            compare(viewport.showViewportAxes, true)
            compare(panel.suppressBackground, false)
        }
    }
}
