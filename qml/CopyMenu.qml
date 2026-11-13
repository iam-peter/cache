import QtQuick
import QtQuick.Controls
import Cache

// The ways to copy notes: their Markdown as it is, a picture of them,
// their text without media, their media alone (the last two for a chat's
// prompt box, which takes text or files in one paste, never both), and
// rich text: HTML with the images inside, for Teams, mail and documents.
Menu {
    id: menu

    required property Item canvas
    title: qsTr("Copy")

    KeyedMenuItem {
        text: qsTr("Raw Markdown")
        onTriggered: menu.canvas.copyNotes("markdown")
    }
    KeyedMenuItem {
        text: qsTr("As image")
        shortcut: "Ctrl+Shift+C"
        onTriggered: menu.canvas.exportImage(menu.canvas.selection, "copy")
    }
    KeyedMenuItem {
        text: qsTr("Text only")
        onTriggered: menu.canvas.copyNotes("text")
    }
    KeyedMenuItem {
        text: qsTr("Media only")
        onTriggered: menu.canvas.copyNotes("media")
    }
    MenuSeparator {}
    // HTML with the images inside, and the plain text: what Teams, mail and
    // office documents take
    KeyedMenuItem {
        text: qsTr("Rich text (HTML)")
        shortcut: "Ctrl+C"
        onTriggered: menu.canvas.copySelected()
    }
}
