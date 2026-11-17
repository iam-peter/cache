import QtQuick
import QtQuick.Controls
import Cache

// A tooltip for something on the canvas, declared inside it: placed above
// (or, without room, below) where that thing is on screen. The attached
// ToolTip lands astray there, as it doesn't follow the canvas's zoom.
Item {
    id: tipper

    property bool shown: false
    property string text
    property int delay: 500
    // What it tells of: the item it is declared in
    readonly property Item target: parent

    ToolTip {
        id: tip
        parent: Overlay.overlay
        visible: tipper.shown && tipper.text !== "" && tipper.target !== null
        delay: tipper.delay
        text: tipper.text
        // Where its item is on screen, asked whenever it shows
        readonly property point above: visible ? tipper.target.mapToItem(Overlay.overlay, tipper.target.width / 2, 0)
                                               : Qt.point(0, 0)
        readonly property point below: visible ? tipper.target.mapToItem(Overlay.overlay, tipper.target.width / 2,
                                                                          tipper.target.height)
                                               : Qt.point(0, 0)
        x: Math.round(Math.max(4, Math.min((parent ? parent.width : 0) - width - 4, above.x - width / 2)))
        y: Math.round(above.y - height - 6 >= 4 ? above.y - height - 6 : below.y + 6)
    }
}
