import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: viewportPanel

    color: "#1a1a2e"

    // Placeholder gradient background
    gradient: Gradient {
        GradientStop { position: 0.0; color: "#1a1a2e" }
        GradientStop { position: 1.0; color: "#16213e" }
    }

    // Placeholder content
    Item {
        anchors.fill: parent

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
                text: "Visualization Viewport"
                color: "#ffffff"
                opacity: 0.2
                font.pixelSize: 16
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: "Vulkan renderer will be initialized here"
                color: "#ffffff"
                opacity: 0.15
                font.pixelSize: 12
                Layout.alignment: Qt.AlignHCenter
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

            ColumnLayout {
                id: infoColumn
                anchors.centerIn: parent
                spacing: 2

                Label {
                    text: "FPS: --"
                    color: "#ffffff"
                    font.pixelSize: 10
                    font.family: "monospace"
                }

                Label {
                    text: "Atoms: 0"
                    color: "#ffffff"
                    font.pixelSize: 10
                    font.family: "monospace"
                }

                Label {
                    text: "Mode: Raster"
                    color: "#ffffff"
                    font.pixelSize: 10
                    font.family: "monospace"
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
                    font.family: "monospace"
                }

                Label {
                    text: "RMB: Pan"
                    color: "#ffffff"
                    font.pixelSize: 10
                    font.family: "monospace"
                }

                Label {
                    text: "Scroll: Zoom"
                    color: "#ffffff"
                    font.pixelSize: 10
                    font.family: "monospace"
                }
            }
        }

        // Axis indicator placeholder (top-left)
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
                anchors.fill: parent
                anchors.margins: 10

                onPaint: {
                    var ctx = getContext("2d");
                    ctx.clearRect(0, 0, width, height);

                    var cx = width / 2;
                    var cy = height / 2;
                    var len = 25;

                    // X axis (red)
                    ctx.strokeStyle = "#ff4444";
                    ctx.lineWidth = 2;
                    ctx.beginPath();
                    ctx.moveTo(cx, cy);
                    ctx.lineTo(cx + len, cy);
                    ctx.stroke();

                    // Y axis (green)
                    ctx.strokeStyle = "#44ff44";
                    ctx.beginPath();
                    ctx.moveTo(cx, cy);
                    ctx.lineTo(cx, cy - len);
                    ctx.stroke();

                    // Z axis (blue) - foreshortened
                    ctx.strokeStyle = "#4444ff";
                    ctx.beginPath();
                    ctx.moveTo(cx, cy);
                    ctx.lineTo(cx - len * 0.5, cy + len * 0.5);
                    ctx.stroke();

                    // Labels
                    ctx.font = "10px sans-serif";
                    ctx.fillStyle = "#ff4444";
                    ctx.fillText("X", cx + len + 3, cy + 4);
                    ctx.fillStyle = "#44ff44";
                    ctx.fillText("Y", cx - 4, cy - len - 3);
                    ctx.fillStyle = "#4444ff";
                    ctx.fillText("Z", cx - len * 0.5 - 10, cy + len * 0.5 + 4);
                }
            }
        }
    }

    // Mouse area for future interaction
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        hoverEnabled: true

        property point lastPos

        onPressed: (mouse) => {
            lastPos = Qt.point(mouse.x, mouse.y);
            // Future: start camera interaction
        }

        onPositionChanged: (mouse) => {
            if (pressed) {
                var dx = mouse.x - lastPos.x;
                var dy = mouse.y - lastPos.y;
                lastPos = Qt.point(mouse.x, mouse.y);
                // Future: update camera based on dx, dy
            }
        }

        onWheel: (wheel) => {
            // Future: zoom camera
            console.log("Zoom:", wheel.angleDelta.y > 0 ? "in" : "out");
        }
    }
}
