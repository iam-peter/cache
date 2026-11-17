import QtQuick
import QtQuick.Controls
import Cache
import IgittStyle as Styles

// A tool button with a Lucide icon, tinted like its text. Small, as most
// sit in the bars above lists and diffs; the main tool bar's are medium.
ToolButton {
    id: button
    property string iconName
    // As Icon's: drawn larger for a zoomed canvas, by an icon of its own
    // (the button's draws at the size it shows)
    property real zoom: 1

    styles: [Styles.ButtonStyle.small]
    icon.source: iconName !== "" && (zoom === 1 || text !== "") ? Icons.source(iconName) : ""
    icon.color: palette.buttonText
    icon.width: 16
    icon.height: 16
    display: text === "" ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon

    Icon {
        visible: button.zoom !== 1 && button.text === "" && button.iconName !== ""
        anchors.centerIn: parent
        name: visible ? button.iconName : ""
        size: button.icon.width
        zoom: button.zoom
        color: button.icon.color
    }
}
