import QtQuick
import QtQuick.Controls
import Cache

// The size pipette: choose width, height or both, then click the item to
// take it from. For the toolbars of one selected item and of several.
IconButton {
    id: pipette

    required property Item canvas
    required property var ids

    iconName: "pipette"
    focusPolicy: Qt.NoFocus
    onClicked: menu.popup(pipette, 0, pipette.height)
    ToolTip.visible: hovered && !menu.visible
    ToolTip.delay: 500
    ToolTip.text: qsTr("Same size as another item: pick what, then click it")

    Menu {
        id: menu
        MenuItem {
            text: qsTr("Same width")
            icon.source: Icons.source("move-horizontal")
            onTriggered: pipette.canvas.startSizePick("width", pipette.ids)
        }
        MenuItem {
            text: qsTr("Same height")
            icon.source: Icons.source("move-vertical")
            onTriggered: pipette.canvas.startSizePick("height", pipette.ids)
        }
        MenuItem {
            text: qsTr("Same width and height")
            icon.source: Icons.source("scaling")
            onTriggered: pipette.canvas.startSizePick("size", pipette.ids)
        }
    }
}
