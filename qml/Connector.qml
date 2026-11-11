import QtQuick
import QtQuick.Shapes
import QtQuick.Controls
import IgittStyle as Styles
import Cache

// A connector between two items, in canvas coordinates. Its ends follow
// the items live, while they are dragged too.
Item {
    id: connector

    required property Item canvas
    property var link: ({})          // as BoardModel::links gives it
    property bool decorative: false  // [[wiki link]] lines: faint, no arrow, not selectable
    // While dragging out a new connector: from an item to a point
    property point loosePoint
    property bool loose: false
    property Item looseTarget: null  // the item under the pointer, if any
    property string looseArrow: "end"

    readonly property Item fromCard: (canvas.cardsVersion, canvas.cards[link.from] || null)
    readonly property Item toCard: loose ? null : (canvas.cardsVersion, canvas.cards[link.to] || null)
    readonly property bool selected: !decorative && canvas.selectedLinkId === link.id && link.id !== undefined
    readonly property bool shown: fromCard !== null && (loose || toCard !== null) && !link.hidden
                                  && fromCard.visible && (loose || toCard.visible)
    readonly property color lineColor: selected ? Theme.accent
                                       : decorative ? Styles.Primitives.alpha(Theme.link, 0.55)
                                       : Theme.lineColor(link.color || "")
    readonly property real lineWidth: decorative ? 1.5 : 2
    readonly property string style: decorative ? "curve" : (link.style || "curve")
    readonly property string arrow: loose ? looseArrow : decorative ? "none" : (link.arrow || "end")

    // Everything below is derived from the two rectangles
    readonly property var fromRect: fromCard ? Qt.rect(fromCard.x, fromCard.y, fromCard.width, fromCard.height)
                                             : Qt.rect(0, 0, 0, 0)
    readonly property var toRect: loose ? (looseTarget ? Qt.rect(looseTarget.x, looseTarget.y, looseTarget.width, looseTarget.height)
                                                       : Qt.rect(loosePoint.x, loosePoint.y, 0, 0))
                                        : toCard ? Qt.rect(toCard.x, toCard.y, toCard.width, toCard.height)
                                                 : Qt.rect(0, 0, 0, 0)
    // While dragging out a new connector: the side it would enter by
    property string looseSide: ""
    readonly property var geometry: shown ? compute(fromRect, toRect, style, link.fromSide || "",
                                                    loose ? looseSide : (link.toSide || "")) : null

    // While one of its ends is being moved, the preview stands in for it
    visible: shown && (decorative || loose || canvas.relinkId !== link.id)
    z: decorative ? -2000 : -1000

    // The point of a side, shifted along it (-1 to 1) when two ends share one
    function anchor(r, side, shift) {
        const t = 0.5 + shift * 0.25
        switch (side) {
        case "top":    return { p: Qt.point(r.x + r.width * t, r.y), n: Qt.point(0, -1) }
        case "bottom": return { p: Qt.point(r.x + r.width * t, r.y + r.height), n: Qt.point(0, 1) }
        case "left":   return { p: Qt.point(r.x, r.y + r.height * t), n: Qt.point(-1, 0) }
        default:       return { p: Qt.point(r.x + r.width, r.y + r.height * t), n: Qt.point(1, 0) }
        }
    }
    // Sides for ends left to the routing. Side by side, rectangles connect
    // sideways (as a mindmap's branches do) unless they are much further
    // apart up and down; one above the other, top to bottom.
    function autoSides(a, b) {
        const ac = Qt.point(a.x + a.width / 2, a.y + a.height / 2)
        const bc = Qt.point(b.x + b.width / 2, b.y + b.height / 2)
        const gapX = Math.max(b.x - (a.x + a.width), a.x - (b.x + b.width))
        const gapY = Math.max(b.y - (a.y + a.height), a.y - (b.y + b.height))
        const horizontal = gapX >= 0 ? gapX * 2 >= gapY : gapX >= gapY
        if (horizontal)
            return bc.x >= ac.x ? ["right", "left"] : ["left", "right"]
        return bc.y >= ac.y ? ["bottom", "top"] : ["top", "bottom"]
    }

    // The route between the two ends, the sides chosen where given
    function compute(a, b, kind, fromSide, toSide) {
        const self = loose ? looseTarget !== null && looseTarget === fromCard : fromCard === toCard
        // A loop with a side left open takes the one next to the other
        const order = ["top", "right", "bottom", "left"]
        let s1, s2
        if (self) {
            s1 = fromSide || (toSide ? order[(order.indexOf(toSide) + 1) % 4] : "right")
            s2 = toSide || order[(order.indexOf(s1) + 3) % 4]
        } else {
            const auto = autoSides(a, b)
            s1 = fromSide || auto[0]
            s2 = toSide || auto[1]
        }
        // Both ends on one side of one item: apart along it
        const shared = self && s1 === s2
        const e1 = anchor(a, s1, shared ? -1 : 0)
        // Dragged to empty space: the end is the pointer, entered head-on
        const e2 = loose && !looseTarget ? { p: Qt.point(b.x, b.y), n: Qt.point(-e1.n.x, -e1.n.y) }
                                         : anchor(b, s2, shared ? 1 : 0)
        const p1 = e1.p, p2 = e2.p, n1 = e1.n, n2 = e2.n
        const dist = Math.hypot(p2.x - p1.x, p2.y - p1.y)
        let path = ""
        let samples = []
        let endDir, startDir
        if (kind === "straight" && !self) {
            path = `M ${p1.x} ${p1.y} L ${p2.x} ${p2.y}`
            samples = [p1, p2]
            const len = Math.max(dist, 0.001)
            endDir = Qt.point((p2.x - p1.x) / len, (p2.y - p1.y) / len)
            startDir = Qt.point(-endDir.x, -endDir.y)
        } else if (kind === "elbow" && !self) {
            // Out of each side a little, then at right angles
            const m = 24
            const s = Qt.point(p1.x + n1.x * m, p1.y + n1.y * m)
            const e = Qt.point(p2.x + n2.x * m, p2.y + n2.y * m)
            const h1 = n1.x !== 0, h2 = n2.x !== 0
            const pts = [p1, s]
            if (h1 && h2) {
                const mx = n1.x === n2.x ? (n1.x > 0 ? Math.max(s.x, e.x) : Math.min(s.x, e.x)) : (s.x + e.x) / 2
                pts.push(Qt.point(mx, s.y), Qt.point(mx, e.y))
            } else if (!h1 && !h2) {
                const my = n1.y === n2.y ? (n1.y > 0 ? Math.max(s.y, e.y) : Math.min(s.y, e.y)) : (s.y + e.y) / 2
                pts.push(Qt.point(s.x, my), Qt.point(e.x, my))
            } else if (h1) {
                pts.push(Qt.point(e.x, s.y))
            } else {
                pts.push(Qt.point(s.x, e.y))
            }
            pts.push(e, p2)
            path = "M " + pts.map(q => `${q.x} ${q.y}`).join(" L ")
            samples = pts
            endDir = Qt.point(-n2.x, -n2.y)
            startDir = Qt.point(-n1.x, -n1.y)
        } else if (self && n1.x === -n2.x && n1.y === -n2.y) {
            // A loop between opposite sides: out of one, over the top (or
            // round the right) and into the other
            const d = Math.max(50, Math.min(a.width, a.height) * 0.5)
            const sideways = n1.x !== 0
            const apex = sideways ? Qt.point(a.x + a.width / 2, a.y - d) : Qt.point(a.x + a.width + d, a.y + a.height / 2)
            const t = sideways ? Qt.point(Math.sign(p2.x - p1.x), 0) : Qt.point(0, Math.sign(p2.y - p1.y))
            const k = (sideways ? a.width : a.height) / 2
            const segments = [[p1, Qt.point(p1.x + n1.x * d, p1.y + n1.y * d), Qt.point(apex.x - t.x * k, apex.y - t.y * k), apex],
                              [apex, Qt.point(apex.x + t.x * k, apex.y + t.y * k), Qt.point(p2.x + n2.x * d, p2.y + n2.y * d), p2]]
            path = `M ${p1.x} ${p1.y}`
            for (const [q0, q1, q2, q3] of segments) {
                path += ` C ${q1.x} ${q1.y} ${q2.x} ${q2.y} ${q3.x} ${q3.y}`
                for (let i = 0; i <= 16; ++i) {
                    const u = i / 16, v = 1 - u
                    samples.push(Qt.point(v*v*v*q0.x + 3*v*v*u*q1.x + 3*v*u*u*q2.x + u*u*u*q3.x,
                                          v*v*v*q0.y + 3*v*v*u*q1.y + 3*v*u*u*q2.y + u*u*u*q3.y))
                }
            }
            endDir = Qt.point(-n2.x, -n2.y)
            startDir = Qt.point(-n1.x, -n1.y)
        } else {
            // A curve leaves and enters at right angles; a loop swings out
            // further, so it shows around its item
            const d = self ? Math.max(60, Math.min(a.width, a.height) * 0.7) : Math.max(30, dist * 0.4)
            const c1 = Qt.point(p1.x + n1.x * d, p1.y + n1.y * d)
            const c2 = Qt.point(p2.x + n2.x * d, p2.y + n2.y * d)
            path = `M ${p1.x} ${p1.y} C ${c1.x} ${c1.y} ${c2.x} ${c2.y} ${p2.x} ${p2.y}`
            for (let i = 0; i <= 24; ++i) {
                const t = i / 24, u = 1 - t
                samples.push(Qt.point(u*u*u*p1.x + 3*u*u*t*c1.x + 3*u*t*t*c2.x + t*t*t*p2.x,
                                      u*u*u*p1.y + 3*u*u*t*c1.y + 3*u*t*t*c2.y + t*t*t*p2.y))
            }
            endDir = Qt.point(-n2.x, -n2.y)
            startDir = Qt.point(-n1.x, -n1.y)
        }
        return { path: path, samples: samples, start: p1, end: p2, startDir: startDir, endDir: endDir,
                 mid: halfway(samples), fromSide: s1, toSide: s2 }
    }

    // The point halfway along a polyline, for the label
    function halfway(pts) {
        let total = 0
        for (let i = 1; i < pts.length; ++i)
            total += Math.hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y)
        let left = total / 2
        for (let i = 1; i < pts.length; ++i) {
            const len = Math.hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y)
            if (len >= left && len > 0) {
                const t = left / len
                return Qt.point(pts[i - 1].x + (pts[i].x - pts[i - 1].x) * t, pts[i - 1].y + (pts[i].y - pts[i - 1].y) * t)
            }
            left -= len
        }
        return pts[0]
    }

    function arrowHead(tip, dir) {
        const len = 11, half = 5
        const bx = tip.x - dir.x * len, by = tip.y - dir.y * len
        const px = -dir.y * half, py = dir.x * half
        return `M ${tip.x} ${tip.y} L ${bx + px} ${by + py} L ${bx - px} ${by - py} Z`
    }

    // Distance from a canvas point to the line, for picking it with the mouse
    function distanceTo(p) {
        if (!geometry)
            return 1e9
        const s = geometry.samples
        let best = 1e9
        for (let i = 1; i < s.length; ++i) {
            const ax = s[i - 1].x, ay = s[i - 1].y, bx = s[i].x, by = s[i].y
            const vx = bx - ax, vy = by - ay
            const len2 = vx * vx + vy * vy
            const t = len2 > 0 ? Math.max(0, Math.min(1, ((p.x - ax) * vx + (p.y - ay) * vy) / len2)) : 0
            best = Math.min(best, Math.hypot(p.x - (ax + t * vx), p.y - (ay + t * vy)))
        }
        return best
    }

    Shape {
        anchors.fill: parent
        visible: connector.geometry !== null

        ShapePath {
            strokeColor: connector.lineColor
            strokeWidth: connector.selected ? connector.lineWidth + 1 : connector.lineWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            strokeStyle: connector.decorative || connector.link.dashed ? ShapePath.DashLine : ShapePath.SolidLine
            dashPattern: connector.decorative ? [1, 3] : [4, 3]
            PathSvg { path: connector.geometry ? connector.geometry.path : "" }
        }
        ShapePath {
            strokeColor: "transparent"
            fillColor: connector.arrow === "none" ? "transparent" : connector.lineColor
            PathSvg {
                path: !connector.geometry || connector.arrow === "none" ? ""
                      : connector.arrowHead(connector.geometry.end, connector.geometry.endDir)
                        + (connector.arrow === "both"
                           ? " " + connector.arrowHead(connector.geometry.start, connector.geometry.startDir) : "")
            }
        }
    }

    // Label in the middle
    Rectangle {
        visible: !connector.decorative && connector.geometry !== null && (connector.link.label || "").length > 0
        x: connector.geometry ? connector.geometry.mid.x - width / 2 : 0
        y: connector.geometry ? connector.geometry.mid.y - height / 2 : 0
        width: label.implicitWidth + 12
        height: label.implicitHeight + 4
        radius: Theme.radius
        color: Theme.canvas
        border.color: connector.selected ? Theme.accent : "transparent"
        Label {
            id: label
            anchors.centerIn: parent
            text: connector.link.label || ""
            color: Theme.textMuted
            font.pixelSize: 12
        }
    }
}
