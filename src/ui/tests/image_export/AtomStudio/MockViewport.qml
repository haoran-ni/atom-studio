import QtQuick

Rectangle {
    property color backgroundColor: "white"
    property bool showViewportAxes: true
    property real viewportAxesX: 0
    property real viewportAxesY: 0
    property real viewportAxesScale: 1
    property real fps: 0
    property int rendererMode: 0
    property int sampleCount: 0
    signal cameraChanged()

    property int frameToken: 0
    property int requestedToken: 1 // An older frame is already in flight.
    property color requestedBackground: "white"
    property bool requestedAxes: true
    property bool displayedAxes: true
    color: "white"

    function requestFrame() {
        requestedBackground = Qt.rgba(backgroundColor.r, backgroundColor.g,
                                      backgroundColor.b, backgroundColor.a)
        requestedAxes = showViewportAxes
        return ++requestedToken
    }

    function presentOldFrame() {
        // The old texture still contains axes and an opaque background.
        frameToken = 1
    }

    function presentRequestedFrame() {
        color = requestedBackground
        displayedAxes = requestedAxes
        frameToken = requestedToken
    }

    function getAxisDirections() { return [1, 0, 0, 0, 1, 0, 0, 0, 1] }

    Rectangle {
        x: 10
        y: 10
        width: 50
        height: 50
        color: "red"
        visible: parent.displayedAxes
    }
}
