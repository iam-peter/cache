import QtQuick
import QtQuick.Controls
import IgittStyle as Styles
import Cache

// Short-lived message at the bottom of the canvas, styled like the
// style's popovers.
Rectangle {
    id: toast

    // `seconds`: longer for a message that says what to do next
    function show(message, seconds) {
        label.text = message
        opacity = 1
        hideTimer.interval = (seconds || 3.2) * 1000
        hideTimer.restart()
    }

    width: Math.min(label.implicitWidth + 2 * Styles.Primitives.spacing.sp200, parent ? parent.width - 40 : 600)
    height: label.implicitHeight + 2 * Styles.Primitives.spacing.sp125
    radius: Theme.radius
    color: Theme.s.background_elevated
    border.color: Theme.s.stroke_subtle
    opacity: 0
    visible: opacity > 0

    Behavior on opacity { NumberAnimation { duration: 160 } }

    Styles.Shadow {
        z: -1
        anchors.fill: parent
        radius: parent.radius
        depth: 3
    }

    Label {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, toast.width - 2 * Styles.Primitives.spacing.sp200)
        elide: Text.ElideRight
    }

    Timer {
        id: hideTimer
        interval: 3200
        onTriggered: toast.opacity = 0
    }
}
