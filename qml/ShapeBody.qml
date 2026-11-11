import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import IgittStyle as Styles
import Cache

// A rectangle, ellipse, diamond or triangle with a label, for flowcharts
// and diagrams.
Item {
    id: body

    required property Item card
    readonly property string kind: card.itemShape || "rect"
    readonly property bool editing: card.editing
    // How much of the width the label may use
    readonly property real inset: kind === "rect" ? 0.9 : kind === "ellipse" ? 0.72 : 0.56

    function outline(k, w, h) {
        switch (k) {
        case "ellipse":
            return `M 0 ${h / 2} A ${w / 2} ${h / 2} 0 1 0 ${w} ${h / 2} A ${w / 2} ${h / 2} 0 1 0 0 ${h / 2} Z`
        case "diamond":
            return `M ${w / 2} 0 L ${w} ${h / 2} L ${w / 2} ${h} L 0 ${h / 2} Z`
        case "triangle":
            return `M ${w / 2} 0 L ${w} ${h} L 0 ${h} Z`
        default: {
            const r = Math.min(8, w / 4, h / 4)
            return `M ${r} 0 L ${w - r} 0 Q ${w} 0 ${w} ${r} L ${w} ${h - r} Q ${w} ${h} ${w - r} ${h}`
                 + ` L ${r} ${h} Q 0 ${h} 0 ${h - r} L 0 ${r} Q 0 0 ${r} 0 Z`
        }
        }
    }

    function commit() {
        if (editor.text !== card.itemText)
            Board.setText(card.index, editor.text)
    }
    onEditingChanged: {
        if (editing) {
            editor.text = card.itemText
            editor.selectAll()
            editor.forceActiveFocus()
        } else {
            commit()
        }
    }

    Shape {
        anchors.fill: parent
        ShapePath {
            fillColor: Theme.shapeColor(body.card.itemColor)
            strokeColor: body.card.selected ? Theme.accent
                         : body.card.itemColor ? Theme.swatch(body.card.itemColor) : Theme.s.stroke_field
            strokeWidth: (body.card.selected ? 2 : 1.5) * body.card.inv
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: body.outline(body.kind, body.width, body.height) }
        }
    }

    Label {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: body.kind === "triangle" ? body.height * 0.15 : 0
        width: body.width * body.inset
        visible: !body.editing
        text: body.card.itemText
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        maximumLineCount: Math.max(1, Math.floor(body.height * 0.8 / 18))
        elide: Text.ElideRight
        font.pixelSize: 14
    }

    TextArea {
        id: editor
        anchors.centerIn: parent
        width: body.width * body.inset
        visible: body.editing
        horizontalAlignment: TextEdit.AlignHCenter
        wrapMode: TextEdit.Wrap
        background: null
        font.pixelSize: 14
        Keys.onEscapePressed: body.card.canvas.finishEditing()
        Keys.onReturnPressed: (event) => {
            if (event.modifiers & Qt.ShiftModifier)
                event.accepted = false // a new line
            else
                body.card.canvas.finishEditing()
        }
    }
}
