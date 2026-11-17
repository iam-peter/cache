import QtQuick
import QtQuick.Controls
import IgittStyle as Styles
import Cache

// A labelled area grouping what lies in it. The title tab sits above the
// top-left corner and keeps its size on screen.
Item {
    id: body

    required property Item card
    readonly property bool editing: card.editing

    function commit() {
        if (titleField.text !== card.itemText)
            Board.setText(card.index, titleField.text)
    }
    onEditingChanged: {
        if (editing) {
            titleField.text = card.itemText
            titleField.selectAll()
            titleField.forceActiveFocus()
        } else {
            commit()
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius * 2
        color: Theme.sectionColor(body.card.itemColor)
        border.width: (body.card.selected ? 2 : 1) * body.card.inv
        border.color: body.card.selected ? Theme.accent
                      : Styles.Primitives.alpha(body.card.itemColor ? Theme.swatch(body.card.itemColor) : Theme.s.stroke_field, 0.7)
    }

    // Title, constant size on screen
    Rectangle {
        id: tab
        scale: body.card.inv
        transformOrigin: Item.BottomLeft
        x: 0
        y: -height - 4 * body.card.inv
        width: titleRow.implicitWidth + 20 + foldButton.width
        height: 28
        radius: Theme.radius
        color: body.card.itemColor ? Theme.swatch(body.card.itemColor) : Theme.s.background_secondary

        Row {
            id: titleRow
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Label {
                visible: !body.editing
                // No wider than the section on screen
                width: Math.min(implicitWidth, Math.max(60, body.width * body.card.canvas.zoom - 20))
                elide: Text.ElideRight
                text: body.card.itemText.length > 0 ? body.card.itemText : qsTr("Section")
                Redacted { cover: tab.color }
                font.weight: Styles.Primitives.fontWeight.semiBold
                color: body.card.itemColor ? Styles.TokenInterface.textOn(Theme.swatch(body.card.itemColor)) : Theme.text
            }
            TextField {
                id: titleField
                visible: body.editing
                width: 220
                height: 24
                topPadding: 0
                bottomPadding: 0
                Keys.onReturnPressed: body.card.canvas.finishEditing()
                Keys.onEscapePressed: body.card.canvas.finishEditing()
            }
        }

        MouseArea { // drags the section, double-click renames
            anchors.fill: parent
            enabled: !body.editing
            onPressed: (mouse) => body.card.pressAt(mapToItem(body.card.parent, mouse.x, mouse.y), mouse.modifiers)
            onPositionChanged: (mouse) => body.card.dragTo(mapToItem(body.card.parent, mouse.x, mouse.y), mouse.modifiers)
            onReleased: body.card.release()
            onCanceled: body.card.release()
            onDoubleClicked: body.card.canvas.startEditing(body.card.itemId)
        }

        // Folding: what lies in it hidden, the section a strip
        Rectangle {
            id: foldButton
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            width: 20
            height: 20
            radius: Theme.radius
            color: foldHover.hovered ? Qt.alpha(Theme.text, 0.12) : "transparent"
            Icon {
                anchors.centerIn: parent
                name: body.card.itemCollapsed ? "chevron-right" : "chevron-down"
                size: 14
                color: body.card.itemColor ? Styles.TokenInterface.textOn(Theme.swatch(body.card.itemColor)) : Theme.text
            }
            HoverHandler { id: foldHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: Board.toggleCollapsed(body.card.itemId) }
            ToolTip.visible: foldHover.hovered
            ToolTip.delay: 400
            ToolTip.text: body.card.itemCollapsed ? qsTr("Unfold the section") : qsTr("Fold the section, hiding what lies in it")
        }
    }

    // Folded: what it holds, said
    Label {
        visible: body.card.itemCollapsed
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: body.card.descendants === 1 ? qsTr("1 item folded away")
                                          : qsTr("%1 items folded away").arg(body.card.descendants)
        color: Theme.textMuted
        font.pixelSize: Styles.Primitives.fontSize.small
    }
}
