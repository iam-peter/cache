import QtQuick
import IgittStyle as Styles
import Cache

// The whole canvas, small, with the part on screen marked. Click or drag
// to go somewhere.
Rectangle {
    id: map

    required property Item canvas

    // The canvas' items and the view, together
    readonly property rect content: {
        Board.count
        const b = Board.itemsBounds()
        const v = canvas.toWorld(0, 0)
        const v2 = canvas.toWorld(canvas.width, canvas.height)
        const x = Math.min(b.x, v.x), y = Math.min(b.y, v.y)
        return Qt.rect(x, y, Math.max(b.x + b.width, v2.x) - x, Math.max(b.y + b.height, v2.y) - y)
    }
    readonly property real scaleFactor: Math.min((width - 12) / Math.max(1, content.width),
                                                 (height - 12) / Math.max(1, content.height))
    readonly property point offset: Qt.point((width - content.width * scaleFactor) / 2,
                                             (height - content.height * scaleFactor) / 2)

    function toMap(x, y) {
        return Qt.point(offset.x + (x - content.x) * scaleFactor, offset.y + (y - content.y) * scaleFactor)
    }
    function fromMap(x, y) {
        return Qt.point(content.x + (x - offset.x) / scaleFactor, content.y + (y - offset.y) / scaleFactor)
    }

    width: 200
    height: 140
    radius: Theme.radius * 2
    color: Styles.Primitives.alpha(Theme.s.background_elevated, 0.92)
    border.color: Theme.s.stroke_subtle
    clip: true

    Repeater {
        model: Board
        delegate: Rectangle {
            required property real posX
            required property real posY
            required property real itemWidth
            required property real itemHeight
            required property string itemType
            required property string itemColor
            required property bool itemHidden
            readonly property point at: map.toMap(posX, posY)
            visible: !itemHidden && itemType !== "stroke"
            x: at.x
            y: at.y
            width: Math.max(2, itemWidth * map.scaleFactor)
            height: Math.max(2, itemHeight * map.scaleFactor)
            radius: 1
            z: itemType === "section" ? -1 : 0
            color: itemType === "section" ? Theme.sectionColor(itemColor)
                   : itemColor ? Theme.swatch(itemColor) : Theme.s.text_muted
            opacity: itemType === "section" ? 1 : 0.7
        }
    }

    // What the canvas shows
    Rectangle {
        readonly property point a: map.toMap(map.canvas.toWorld(0, 0).x, map.canvas.toWorld(0, 0).y)
        readonly property point b: map.toMap(map.canvas.toWorld(map.canvas.width, map.canvas.height).x,
                                             map.canvas.toWorld(map.canvas.width, map.canvas.height).y)
        x: a.x
        y: a.y
        width: b.x - a.x
        height: b.y - a.y
        color: Styles.Primitives.alpha(Theme.accent, 0.12)
        border.color: Theme.accent
        border.width: 1.5
        radius: 2
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        function go(mouse) {
            const p = map.fromMap(mouse.x, mouse.y)
            map.canvas.panTo(p.x, p.y)
        }
        onPressed: (mouse) => go(mouse)
        onPositionChanged: (mouse) => { if (pressed) go(mouse) }
    }
}
