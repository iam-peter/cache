import QtQuick
import QtQuick.Controls
import Cache

// The ways to copy notes: their Markdown as it is, a picture of them,
// their text without media, their media alone (the last two for a chat's
// prompt box, which takes text or files in one paste, never both), and
// rich text with images for Teams and mail.
Menu {
    id: menu

    required property Item canvas
    title: qsTr("Copy")

    MenuItem {
        text: qsTr("Raw Markdown")
        onTriggered: menu.canvas.copyNotes("markdown")
    }
    MenuItem {
        text: qsTr("As image  (Ctrl+Shift+C)")
        onTriggered: menu.canvas.exportImage(menu.canvas.selection, "copy")
    }
    MenuItem {
        text: qsTr("Text only")
        onTriggered: menu.canvas.copyNotes("text")
    }
    MenuItem {
        text: qsTr("Media only")
        onTriggered: menu.canvas.copyNotes("media")
    }
    MenuSeparator {}
    MenuItem {
        text: qsTr("With images, for Teams and mail  (Ctrl+C)")
        onTriggered: menu.canvas.copySelected()
    }
}
