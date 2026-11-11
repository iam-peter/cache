import QtQuick
import QtQuick.Shapes
import Cache

// A pen or highlighter line.
Item {
    id: body

    required property Item card
    readonly property bool highlighter: card.itemShape === "highlighter"

    // Whether a point (in card coordinates) is on the line, so clicks beside
    // a thin line reach what is under it
    function hit(p) {
        const pts = card.itemPoints
        const slack = card.itemSize / 2 + 6 * card.inv
        for (let i = 1; i < pts.length; ++i) {
            const ax = pts[i - 1].x, ay = pts[i - 1].y, vx = pts[i].x - ax, vy = pts[i].y - ay
            const len2 = vx * vx + vy * vy
            const t = len2 > 0 ? Math.max(0, Math.min(1, ((p.x - ax) * vx + (p.y - ay) * vy) / len2)) : 0
            if (Math.hypot(p.x - (ax + t * vx), p.y - (ay + t * vy)) <= slack)
                return true
        }
        return false
    }

    Shape {
        anchors.fill: parent
        opacity: body.highlighter ? 0.38 : 1
        ShapePath {
            strokeColor: body.card.selected ? Theme.accent
                         : body.highlighter ? Theme.swatch(body.card.itemColor || "yellow")
                         : Theme.penColor(body.card.itemColor)
            strokeWidth: body.card.itemSize > 0 ? body.card.itemSize : 3
            fillColor: "transparent"
            capStyle: body.highlighter ? ShapePath.FlatCap : ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathPolyline { path: body.card.itemPoints }
        }
    }
}
