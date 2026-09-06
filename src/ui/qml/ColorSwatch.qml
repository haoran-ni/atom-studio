import QtQuick

Item {
    id: root

    property color color: "white"
    property real radius: 6
    property color borderColor: "#c7c9d1"
    property real borderWidth: 1

    // A cached transparency pattern, repainted only on size/shape changes.
    Canvas {
        id: checkerboard
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const r = Math.min(root.radius, width / 2, height / 2)
            ctx.beginPath()
            ctx.moveTo(r, 0)
            ctx.arcTo(width, 0, width, height, r)
            ctx.arcTo(width, height, 0, height, r)
            ctx.arcTo(0, height, 0, 0, r)
            ctx.arcTo(0, 0, width, 0, r)
            ctx.closePath()
            ctx.clip()
            ctx.fillStyle = "#ffffff"
            ctx.fillRect(0, 0, width, height)
            ctx.fillStyle = "#d6d6da"
            for (let y = 0; y < height; y += 6) {
                for (let x = 0; x < width; x += 6) {
                    if ((Math.floor(x / 6) + Math.floor(y / 6)) % 2 === 0)
                        ctx.fillRect(x, y, 6, 6)
                }
            }
        }
    }

    onRadiusChanged: checkerboard.requestPaint()

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.color
        border.color: root.borderColor
        border.width: root.borderWidth
    }
}
