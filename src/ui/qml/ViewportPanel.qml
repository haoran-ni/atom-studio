import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AtomStudio 1.0

Rectangle {
    id: viewportPanel

    property var viewport: viewportLoader.item

    AppMenuActions { id: appActions }

    color: "#e6e6e6"

    // Placeholder gradient background (shown when no structure is loaded)
    gradient: Gradient {
        GradientStop { position: 0.0; color: "#e6e6e6" }
        GradientStop { position: 1.0; color: "#e6e6e6" }
    }

    // Platform-conditional viewport: Metal on macOS, OpenGL elsewhere
    Loader {
        id: viewportLoader
        anchors.fill: parent
        sourceComponent: Qt.platform.os === "osx" ? metalViewportComp : openglViewportComp
    }

    Component {
        id: openglViewportComp
        OpenGLViewport {}
    }

    Component {
        id: metalViewportComp
        MetalViewport {}
    }

    // Floating tab bar with menu actions (replaces the old window header menu)
    Rectangle {
        id: floatingTabBar
        z: 50
        color: "#2d2d2d"
        radius: 10
        border.color: "#4a4a4a"
        border.width: 1
        height: 38
        width: tabRow.implicitWidth + 16

        property bool dragArmed: false
        property bool positioned: false
        property bool userMoved: false

        function clampToBounds() {
            x = Math.max(0, Math.min(x, Math.max(0, viewportPanel.width - width)))
            y = Math.max(0, Math.min(y, Math.max(0, viewportPanel.height - height)))
        }

        function placeDefaultPosition() {
            if (viewportPanel.width <= 0 || viewportPanel.height <= 0 || width <= 0 || height <= 0) {
                return
            }

            x = Math.round((viewportPanel.width - width) * 0.5)
            y = Math.round(viewportPanel.height * 0.03)
            positioned = true
            clampToBounds()
        }

        function showMenu(menu, button) {
            menu.x = Math.round(floatingTabBar.x + tabRow.x + button.x)
            menu.y = Math.round(floatingTabBar.y + floatingTabBar.height + 4)
            menu.open()
        }

        Component.onCompleted: placeDefaultPosition()

        onWidthChanged: {
            if (!userMoved) {
                placeDefaultPosition()
            } else if (positioned) {
                clampToBounds()
            }
        }

        onHeightChanged: {
            if (!userMoved) {
                placeDefaultPosition()
            } else if (positioned) {
                clampToBounds()
            }
        }

        RowLayout {
            id: tabRow
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 4

            Rectangle {
                id: dragHandle
                Layout.preferredWidth: 18
                Layout.preferredHeight: Math.max(20, floatingTabBar.height - 10)
                Layout.alignment: Qt.AlignVCenter
                radius: 6
                color: dragMouseArea.containsPress ? "#555555" : "#3a3a3a"
                ToolTip.visible: dragMouseArea.containsMouse
                ToolTip.text: qsTr("Hold and drag to move.\nDouble-click to reset.")

                Label {
                    anchors.centerIn: parent
                    text: "||"
                    color: "#a0a0a0"
                    font.pixelSize: 10
                }

                MouseArea {
                    id: dragMouseArea
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    hoverEnabled: true
                    pressAndHoldInterval: 350
                    cursorShape: floatingTabBar.dragArmed ? Qt.ClosedHandCursor : Qt.OpenHandCursor

                    drag.target: floatingTabBar.dragArmed ? floatingTabBar : undefined
                    drag.axis: Drag.XAndYAxis
                    drag.minimumX: 0
                    drag.maximumX: Math.max(0, viewportPanel.width - floatingTabBar.width)
                    drag.minimumY: 0
                    drag.maximumY: Math.max(0, viewportPanel.height - floatingTabBar.height)

                    onPressed: floatingTabBar.dragArmed = false
                    onPressAndHold: floatingTabBar.dragArmed = true
                    onPositionChanged: {
                        if (floatingTabBar.dragArmed && drag.active) {
                            floatingTabBar.userMoved = true
                        }
                    }
                    onDoubleClicked: {
                        floatingTabBar.dragArmed = false
                        floatingTabBar.userMoved = false
                        floatingTabBar.placeDefaultPosition()
                    }
                    onReleased: floatingTabBar.dragArmed = false
                    onCanceled: floatingTabBar.dragArmed = false
                }
            }

            ToolButton {
                id: fileTabButton
                text: qsTr("File")
                onClicked: floatingTabBar.showMenu(fileMenu, fileTabButton)
            }

            ToolButton {
                id: editTabButton
                text: qsTr("Edit")
                onClicked: floatingTabBar.showMenu(editMenu, editTabButton)
            }
        }
    }

    Menu {
        id: fileMenu
        parent: viewportPanel

        MenuItem { action: appActions.fileOpen }
        MenuItem { action: appActions.fileOpenRecent }
        MenuSeparator {}
        MenuItem { action: appActions.fileSaveImage }
        MenuItem { action: appActions.fileExport }
        MenuSeparator {}
        MenuItem { action: appActions.fileQuit }
    }

    Menu {
        id: editMenu
        parent: viewportPanel

        MenuItem { action: appActions.editUndo }
        MenuItem { action: appActions.editRedo }
        MenuSeparator {}
        MenuItem { action: appActions.editSelectAll }
        MenuItem { action: appActions.editDeselectAll }
        MenuSeparator {}
        MenuItem { action: appActions.editPreferences }
    }

    // Placeholder content (shown when no structure loaded)
    Item {
        anchors.fill: parent
        visible: !StructureModel.hasStructure

        // Center placeholder text
        ColumnLayout {
            anchors.centerIn: parent
            spacing: 15

            Label {
                text: "ATOM STUDIO"
                color: "#000000"
                font.pixelSize: 40
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
        }
    }

    // Viewport info overlay (top-right)
    InfoOverlayBox {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        visible: StructureModel.hasStructure

        Label {
            text: "FPS: " + (viewportPanel.viewport ? viewportPanel.viewport.fps.toFixed(1) : "0.0")
            color: "#ffffff"
            font.pixelSize: 10
        }

        Label {
            visible: viewportPanel.viewport ? viewportPanel.viewport.rendererMode === 1 : false
            text: "Mode: Ray Tracing"
            color: "#88ccff"
            font.pixelSize: 10
        }

        Label {
            visible: viewportPanel.viewport ? viewportPanel.viewport.rendererMode === 1 : false
            text: "Samples: " + (viewportPanel.viewport ? viewportPanel.viewport.sampleCount : 0)
            color: "#88ccff"
            font.pixelSize: 10
        }
    }

    // Camera controls hint (bottom-right)
    InfoOverlayBox {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10

        Label { text: "LMB: Rotate";   color: "#ffffff"; font.pixelSize: 10 }
        Label { text: "RMB: Pan";      color: "#ffffff"; font.pixelSize: 10 }
        Label { text: "Scroll: Zoom";  color: "#ffffff"; font.pixelSize: 10 }
        Label { text: "DblClick: Reset"; color: "#ffffff"; font.pixelSize: 10 }
    }

    // Axis indicator (UI overlay only; not part of raster/ray tracing)
    Item {
        id: axisOverlay
        z: 40
        width: 0
        height: 0

        property bool positioned: false
        property real axisScale: 1.0
        property real minAxisScale: 0.6
        property real maxAxisScale: 2.0
        property real hoverScaleBoost: 1.12
        property real edgeMargin: 10
        property real baseLength: 68
        property real baseArrowLength: 5
        property real baseLineWidth: 2
        property real baseLabelOffset: 8
        property real baseFontSize: 18
        property real interactionPadding: 12
        property bool selected: axisMouseArea.containsMouse || axisMouseArea.pressed
        property real visualScale: axisScale * (selected ? hoverScaleBoost : 1.0)
        property real visualExtent: (baseLength + baseLabelOffset + baseArrowLength + baseFontSize) * visualScale
        property real boundsRadius: visualExtent + 4
        // Reserve space for the hover enlargement so hover does not change the
        // persisted overlay position via bounds clamping.
        property real layoutScale: axisScale * hoverScaleBoost
        property real layoutExtent: (baseLength + baseLabelOffset + baseArrowLength + baseFontSize) * layoutScale
        property real layoutBoundsRadius: layoutExtent + 4
        // Keep the drag/selection hit area closer to the visible axes body/arrowheads
        // instead of the full label/canvas bounds. Use a hover-independent radius
        // (reserve hover size) to avoid containsMouse <-> hit-size flicker loops.
        property real hitScale: axisScale * hoverScaleBoost
        property real hitVisualRadius: (baseLength + baseArrowLength + 8) * hitScale
        property real hitRadius: Math.max(hitVisualRadius + interactionPadding, 34)
        property real canvasHalfSize: Math.max(boundsRadius + 20, 70)

        function clampToBounds() {
            if (viewportPanel.width <= 0 || viewportPanel.height <= 0) {
                return
            }

            var minX = layoutBoundsRadius + edgeMargin
            var maxX = Math.max(minX, viewportPanel.width - layoutBoundsRadius - edgeMargin)
            var minY = layoutBoundsRadius + edgeMargin
            var maxY = Math.max(minY, viewportPanel.height - layoutBoundsRadius - edgeMargin)

            x = Math.max(minX, Math.min(x, maxX))
            y = Math.max(minY, Math.min(y, maxY))
        }

        function placeDefaultPosition() {
            if (viewportPanel.width <= 0 || viewportPanel.height <= 0) {
                return
            }

            x = layoutBoundsRadius + edgeMargin
            y = viewportPanel.height - layoutBoundsRadius - edgeMargin
            positioned = true
            clampToBounds()
        }

        function syncBackendAxesState() {
            if (!viewportPanel.viewport) {
                return
            }

            viewportPanel.viewport.viewportAxesX = x
            viewportPanel.viewport.viewportAxesY = y
            viewportPanel.viewport.viewportAxesScale = visualScale
        }

        Component.onCompleted: {
            placeDefaultPosition()
            syncBackendAxesState()
            axisCanvas.requestPaint()
        }

        onXChanged: syncBackendAxesState()
        onYChanged: syncBackendAxesState()
        onAxisScaleChanged: {
            if (positioned) {
                clampToBounds()
            }
        }

        onVisualScaleChanged: {
            syncBackendAxesState()
            axisCanvas.requestPaint()
        }

        Connections {
            target: viewportPanel
            function onViewportChanged() {
                axisOverlay.syncBackendAxesState()
                axisCanvas.requestPaint()
            }
        }

        Canvas {
            id: axisCanvas
            x: -width / 2
            y: -height / 2
            width: axisOverlay.canvasHalfSize * 2
            height: axisOverlay.canvasHalfSize * 2

            Connections {
                target: viewportPanel.viewport
                function onCameraChanged() { axisCanvas.requestPaint() }
            }

            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);

                // Get camera-projected axis directions from the view matrix
                if (!viewportPanel.viewport) return;

                var cx = width / 2;
                var cy = height / 2;
                var scale = axisOverlay.visualScale;
                var len = axisOverlay.baseLength * scale;
                var labelOffset = axisOverlay.baseLabelOffset * scale;
                var fontSize = Math.max(8, Math.round(axisOverlay.baseFontSize * scale));

                var d = viewportPanel.viewport.getAxisDirections();
                // d = [Xx, Xy, Xz,  Yx, Yy, Yz,  Zx, Zy, Zz]
                var axes = [
                    { axisId: 0, dx: d[0], dy: d[1], z: d[2], color: "#ff4444", label: "X" },
                    { axisId: 1, dx: d[3], dy: d[4], z: d[5], color: "#44ff44", label: "Y" },
                    { axisId: 2, dx: d[6], dy: d[7], z: d[8], color: "#4444ff", label: "Z" }
                ];

                axes.sort(function(a, b) { return a.z - b.z })

                ctx.font = "bold " + fontSize + "px sans-serif";
                ctx.textAlign = "center";
                ctx.textBaseline = "middle";

                for (var i = 0; i < axes.length; i++) {
                    var ax = axes[i]
                    // Hide labels when the corresponding axis points strongly into the screen.
                    if (ax.z < -0.95) {
                        continue
                    }
                    var lx = cx + ax.dx * (len + labelOffset)
                    var ly = cy + ax.dy * (len + labelOffset)
                    ctx.fillStyle = ax.color
                    ctx.fillText(ax.label, lx, ly)
                }
            }
        }

        MouseArea {
            id: axisMouseArea
            z: 1
            x: -width / 2
            y: -height / 2
            width: axisOverlay.hitRadius * 2
            height: axisOverlay.hitRadius * 2
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton
            preventStealing: true
            cursorShape: pressed ? Qt.ClosedHandCursor : (containsMouse ? Qt.OpenHandCursor : Qt.ArrowCursor)

            drag.target: axisOverlay
            drag.axis: Drag.XAndYAxis
            drag.minimumX: axisOverlay.layoutBoundsRadius + axisOverlay.edgeMargin
            drag.maximumX: Math.max(drag.minimumX, viewportPanel.width - axisOverlay.layoutBoundsRadius - axisOverlay.edgeMargin)
            drag.minimumY: axisOverlay.layoutBoundsRadius + axisOverlay.edgeMargin
            drag.maximumY: Math.max(drag.minimumY, viewportPanel.height - axisOverlay.layoutBoundsRadius - axisOverlay.edgeMargin)

            onContainsMouseChanged: axisCanvas.requestPaint()
            onPressed: axisCanvas.requestPaint()
            onReleased: axisCanvas.requestPaint()
            onCanceled: axisCanvas.requestPaint()

            onWheel: (wheel) => {
                var deltaY = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.pixelDelta.y
                if (deltaY === 0) {
                    return
                }

                var nextScale = axisOverlay.axisScale + (deltaY > 0 ? 0.1 : -0.1)
                axisOverlay.axisScale = Math.max(axisOverlay.minAxisScale,
                                                 Math.min(axisOverlay.maxAxisScale, nextScale))
                axisOverlay.clampToBounds()
                axisCanvas.requestPaint()
                wheel.accepted = true
            }
        }

        Rectangle {
            id: axisHintBubble
            z: 2
            visible: axisMouseArea.containsMouse && !axisMouseArea.pressed
            x: axisMouseArea.x + axisMouseArea.width * 0.5 + 10
            y: axisMouseArea.y - height - 8
            width: axisHintText.implicitWidth + 12
            height: axisHintText.implicitHeight + 8
            color: "#000000"
            opacity: 0.75
            radius: 4

            Label {
                id: axisHintText
                anchors.centerIn: parent
                text: "Left click to move. Scroll to change size."
                color: "#ffffff"
                font.pixelSize: 10
            }
        }
    }

    // Drop area for files
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            if (drop.hasUrls) {
                FileController.loadFileUrl(drop.urls[0])
            }
        }
    }

    onWidthChanged: {
        if (!floatingTabBar.userMoved || !floatingTabBar.positioned) {
            floatingTabBar.placeDefaultPosition()
        } else if (floatingTabBar.positioned) {
            floatingTabBar.clampToBounds()
        }

        if (!axisOverlay.positioned) {
            axisOverlay.placeDefaultPosition()
        } else {
            axisOverlay.clampToBounds()
        }
    }

    onHeightChanged: {
        if (!floatingTabBar.userMoved || !floatingTabBar.positioned) {
            floatingTabBar.placeDefaultPosition()
        } else if (floatingTabBar.positioned) {
            floatingTabBar.clampToBounds()
        }

        if (!axisOverlay.positioned) {
            axisOverlay.placeDefaultPosition()
        } else {
            axisOverlay.clampToBounds()
        }
    }
}
