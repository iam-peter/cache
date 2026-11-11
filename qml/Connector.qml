import QtQuick
import QtQuick.Shapes
import QtQuick.Controls
import IgittStyle as Styles
import NoteApp

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
    readonly property string arrow: decorative || loose ? (loose ? "end" : "none") : (link.arrow || "end")

    // Everything below is derived from the two rectangles
    readonly property var fromRect: fromCard ? Qt.rect(fromCard.x, fromCard.y, fromCard.width, fromCard.height)
                                             : Qt.rect(0, 0, 0, 0)
    readonly property var toRect: loose ? Qt.rect(loosePoint.x, loosePoint.y, 0, 0)
                                        : toCard ? Qt.rect(toCard.x, toCard.y, toCard.width, toCard.height)
                                                 : Qt.rect(0, 0, 0, 0)
    readonly property var geometry: shown ? compute(fromRect, toRect, style) : null

    visible: shown
    z: decorative ? -2000 : -1000

    // Ends on the facing sides of the two rectangles; a curve leaves and
    // enters them at right angles, an elbow turns halfway
    function compute(a, b, kind) {
        const ac = Qt.point(a.x + a.width / 2, a.y + a.height / 2)
        const bc = Qt.point(b.x + b.width / 2, b.y + b.height / 2)
        const dx = bc.x - ac.x
        const dy = bc.y - ac.y
        // Side by side, they connect sideways (as a mindmap's branches do)
        // unless they are much further apart up and down; one above the
        // other, they connect top to bottom
        const gapX = Math.max(b.x - (a.x + a.width), a.x - (b.x + b.width))
        const gapY = Math.max(b.y - (a.y + a.height), a.y - (b.y + b.height))
        const horizontal = gapX >= 0 ? gapX * 2 >= gapY : gapX >= gapY
        let p1, p2, n1, n2
        if (horizontal) {
            const right = dx >= 0
            p1 = Qt.point(right ? a.x + a.width : a.x, ac.y)
            p2 = Qt.point(right ? b.x : b.x + b.width, bc.y)
            n1 = Qt.point(right ? 1 : -1, 0)
            n2 = Qt.point(right ? -1 : 1, 0)
        } else {
            const down = dy >= 0
            p1 = Qt.point(ac.x, down ? a.y + a.height : a.y)
            p2 = Qt.point(bc.x, down ? b.y : b.y + b.height)
            n1 = Qt.point(0, down ? 1 : -1)
            n2 = Qt.point(0, down ? -1 : 1)
        }
        const dist = Math.hypot(p2.x - p1.x, p2.y - p1.y)
        let path = ""
        let samples = []
        let endDir, startDir
        if (kind === "straight") {
            path = `M ${p1.x} ${p1.y} L ${p2.x} ${p2.y}`
            samples = [p1, p2]
            const len = Math.max(dist, 0.001)
            endDir = Qt.point((p2.x - p1.x) / len, (p2.y - p1.y) / len)
            startDir = Qt.point(-endDir.x, -endDir.y)
        } else if (kind === "elbow") {
            let m1, m2
            if (horizontal) {
                const mx = (p1.x + p2.x) / 2
                m1 = Qt.point(mx, p1.y)
                m2 = Qt.point(mx, p2.y)
            } else {
                const my = (p1.y + p2.y) / 2
                m1 = Qt.point(p1.x, my)
                m2 = Qt.point(p2.x, my)
            }
            path = `M ${p1.x} ${p1.y} L ${m1.x} ${m1.y} L ${m2.x} ${m2.y} L ${p2.x} ${p2.y}`
            samples = [p1, m1, m2, p2]
            endDir = Qt.point(-n2.x, -n2.y)
            startDir = Qt.point(-n1.x, -n1.y)
        } else {
            const d = Math.max(30, dist * 0.4)
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
        // The middle, for the label
        let mid = samples[Math.floor(samples.length / 2)]
        if (samples.length === 2)
            mid = Qt.point((p1.x + p2.x) / 2, (p1.y + p2.y) / 2)
        else if (samples.length === 4)
            mid = Qt.point((samples[1].x + samples[2].x) / 2, (samples[1].y + samples[2].y) / 2)
        return { path: path, samples: samples, start: p1, end: p2, startDir: startDir, endDir: endDir, mid: mid }
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
