import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AtomStudio 1.0

Rectangle {
    id: viewportPanel

    property var viewport: viewportLoader.item

    color: "#1a1a2e"

    // Placeholder gradient background (shown when no structure is loaded)
    gradient: Gradient {
        GradientStop { position: 0.0; color: "#1a1a2e" }
        GradientStop { position: 1.0; color: "#16213e" }
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

    // Placeholder content (shown when no structure loaded)
    Item {
        anchors.fill: parent
        visible: !StructureModel.hasStructure

        // Grid pattern to show the viewport area
        Canvas {
            id: gridCanvas
            anchors.fill: parent
            opacity: 0.1

            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                ctx.strokeStyle = "#ffffff";
                ctx.lineWidth = 1;

                var gridSize = 50;

                // Draw vertical lines
                for (var x = 0; x <= width; x += gridSize) {
                    ctx.beginPath();
                    ctx.moveTo(x, 0);
                    ctx.lineTo(x, height);
                    ctx.stroke();
                }

                // Draw horizontal lines
                for (var y = 0; y <= height; y += gridSize) {
                    ctx.beginPath();
                    ctx.moveTo(0, y);
                    ctx.lineTo(width, y);
                    ctx.stroke();
                }
            }

            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }

        // Center placeholder text
        ColumnLayout {
            anchors.centerIn: parent
            spacing: 15

            Label {
                text: "ATOM-STUDIO"
                color: "#ffffff"
                opacity: 0.3
                font.pixelSize: 36
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: "Drop a file or use File > Open"
                color: "#ffffff"
                opacity: 0.2
                font.pixelSize: 16
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: "Supported: XYZ, LAMMPS dump, CIF"
                color: "#ffffff"
                opacity: 0.15
                font.pixelSize: 12
                Layout.alignment: Qt.AlignHCenter
            }
        }
    }

    // Viewport info overlay (bottom-left)
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 10
        width: infoColumn.width + 16
        height: infoColumn.height + 12
        color: "#000000"
        opacity: 0.5
        radius: 4
        visible: StructureModel.hasStructure

        ColumnLayout {
            id: infoColumn
            anchors.centerIn: parent
            spacing: 2

            Label {
                text: "FPS: " + (viewportPanel.viewport ? viewportPanel.viewport.fps.toFixed(1) : "0.0")
                color: "#ffffff"
                font.pixelSize: 10
            }

            Label {
                text: "Atoms: " + (viewportPanel.viewport ? viewportPanel.viewport.atomCount : 0)
                color: "#ffffff"
                font.pixelSize: 10
            }

            Label {
                text: "Bonds: " + (viewportPanel.viewport ? viewportPanel.viewport.bondCount : 0)
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
    }

    // Camera controls hint (bottom-right)
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        width: controlsColumn.width + 16
        height: controlsColumn.height + 12
        color: "#000000"
        opacity: 0.5
        radius: 4

        ColumnLayout {
            id: controlsColumn
            anchors.centerIn: parent
            spacing: 2

            Label {
                text: "LMB: Rotate"
                color: "#ffffff"
                font.pixelSize: 10
            }

            Label {
                text: "RMB: Pan"
                color: "#ffffff"
                font.pixelSize: 10
            }

            Label {
                text: "Scroll: Zoom"
                color: "#ffffff"
                font.pixelSize: 10
            }

            Label {
                text: "DblClick: Reset"
                color: "#ffffff"
                font.pixelSize: 10
            }
        }
    }

    // Axis indicator (top-left) — rotates with the camera
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        width: 80
        height: 80
        color: "#000000"
        opacity: 0.3
        radius: 4

        Canvas {
            id: axisCanvas
            anchors.fill: parent
            anchors.margins: 10

            Connections {
                target: viewportPanel.viewport
                function onCameraChanged() { axisCanvas.requestPaint() }
            }

            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);

                var cx = width / 2;
                var cy = height / 2;
                var len = 25;

                // Get camera-projected axis directions from the view matrix
                if (!viewportPanel.viewport) return;
                var d = viewportPanel.viewport.getAxisDirections();
                // d = [Xx, Xy, Xz,  Yx, Yy, Yz,  Zx, Zy, Zz]
                var axes = [
                    { dx: d[0], dy: d[1], z: d[2], color: "#ff4444", label: "X" },
                    { dx: d[3], dy: d[4], z: d[5], color: "#44ff44", label: "Y" },
                    { dx: d[6], dy: d[7], z: d[8], color: "#4444ff", label: "Z" }
                ];

                // Sort ascending by z: draw into-screen axes first,
                // toward-camera axes last (on top)
                axes.sort(function(a, b) { return a.z - b.z; });

                for (var i = 0; i < 3; i++) {
                    var ax = axes[i];
                    var ex = cx + ax.dx * len;
                    var ey = cy + ax.dy * len;

                    // Axis line
                    ctx.strokeStyle = ax.color;
                    ctx.lineWidth = 2;
                    ctx.beginPath();
                    ctx.moveTo(cx, cy);
                    ctx.lineTo(ex, ey);
                    ctx.stroke();

                    // Small arrowhead at the tip
                    var aLen = 5;
                    var norm = Math.sqrt(ax.dx * ax.dx + ax.dy * ax.dy);
                    if (norm > 0.01) {
                        var ndx = ax.dx / norm;
                        var ndy = ax.dy / norm;
                        // Perpendicular direction
                        var px = -ndy;
                        var py = ndx;
                        ctx.beginPath();
                        ctx.moveTo(ex, ey);
                        ctx.lineTo(ex - ndx * aLen + px * aLen * 0.4,
                                   ey - ndy * aLen + py * aLen * 0.4);
                        ctx.moveTo(ex, ey);
                        ctx.lineTo(ex - ndx * aLen - px * aLen * 0.4,
                                   ey - ndy * aLen - py * aLen * 0.4);
                        ctx.stroke();
                    }

                    // Label just past the endpoint
                    ctx.font = "bold 10px sans-serif";
                    ctx.textAlign = "center";
                    ctx.textBaseline = "middle";
                    ctx.fillStyle = ax.color;
                    ctx.fillText(ax.label, cx + ax.dx * (len + 10),
                                           cy + ax.dy * (len + 10));
                }
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
}
