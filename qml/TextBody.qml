import QtQuick
import QtQuick.Controls
import NoteApp

// Free text on the canvas, without a card around it: headings, labels.
Item {
    id: body

    required property Item card
    readonly property bool editing: card.editing
    readonly property real fontSize: card.itemSize > 0 ? card.itemSize : 24

    // The card grows to fit the text when editing ends
    function commit() {
        const text = editor.text
        const height = Math.max(fontSize * 1.4, editor.contentHeight + 8)
        if (text !== card.itemText)
            Board.setText(card.index, text)
        if (Math.abs(height - card.height) > 2)
            Board.setGeometry(card.index, card.x, card.y, card.width, height)
    }
    onEditingChanged: {
        if (editing) {
            editor.text = card.itemText
            editor.cursorPosition = editor.length
            editor.forceActiveFocus()
        } else {
            commit()
        }
    }

    Text {
        anchors.fill: parent
        anchors.topMargin: 4
        visible: !body.editing
        text: body.card.itemText.length > 0 ? body.card.itemText : qsTr("Text")
        wrapMode: Text.Wrap
        color: body.card.itemText.length > 0 ? Theme.penColor(body.card.itemColor) : Theme.textMuted
        font.pixelSize: body.fontSize
        font.weight: body.fontSize >= 32 ? Font.Bold : body.fontSize >= 20 ? Font.DemiBold : Font.Normal
    }

    TextArea {
        id: editor
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        visible: body.editing
        padding: 0
        topPadding: 4
        wrapMode: TextEdit.Wrap
        background: null
        color: Theme.penColor(body.card.itemColor)
        font.pixelSize: body.fontSize
        font.weight: body.fontSize >= 32 ? Font.Bold : body.fontSize >= 20 ? Font.DemiBold : Font.Normal
        Keys.onEscapePressed: body.card.canvas.finishEditing()
    }
}
