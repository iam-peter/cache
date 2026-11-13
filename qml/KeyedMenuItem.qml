import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Cache

// A menu entry with its shortcut in a column of its own on the right,
// quieter than the entry's text
MenuItem {
    id: item

    // As Qt writes it, "Ctrl+Shift+C"; shown with Theme.keys
    property string shortcut

    contentItem: Item {
        implicitWidth: label.implicitWidth + (keysLabel.text ? keysLabel.implicitWidth + 32 : 0)
        implicitHeight: Math.max(label.implicitHeight, keysLabel.implicitHeight)

        IconLabel {
            id: label
            anchors.left: parent.left
            anchors.right: keysLabel.left
            anchors.rightMargin: keysLabel.text ? 16 : 0
            anchors.verticalCenter: parent.verticalCenter
            spacing: item.spacing
            display: item.display
            alignment: Qt.AlignLeft
            icon.source: item.icon.source
            icon.width: item.icon.width
            icon.height: item.icon.height
            icon.color: item.styleCascade.icon.color
            text: item.text
            font: item.font
            color: item.styleCascade.text.color
        }
        Label {
            id: keysLabel
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: item.shortcut ? Theme.keys(item.shortcut) : ""
            font: item.font
            // The entry's own colour, faded: readable on a highlighted row
            // too, without competing with the text
            color: Qt.alpha(item.styleCascade.text.color, 0.5)
        }
    }
}
