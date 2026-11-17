import QtQuick
import QtQuick.Controls.impl
import Cache
import IgittStyle as Styles

// A Lucide icon on its own, tinted to `color`. Drawn by the image the
// controls draw their icons with: a button would take the style's colour
// and ignore `color` (white on a video came out dark in light themes).
Item {
    id: icon

    required property string name
    property color color: Styles.TokenInterface.semantics.text_default
    property int size: 16
    // Drawn this many times larger than it shows: an icon on the canvas,
    // which the canvas's zoom enlarges (Theme.iconZoom)
    property real zoom: 1

    implicitWidth: size
    implicitHeight: size

    // Only with an icon: an IconImage without one asks the desktop's icon
    // theme for a nameless icon, which searches every folder of it
    Loader {
        anchors.fill: parent
        active: icon.name !== ""
        sourceComponent: IconImage {
            source: Icons.source(icon.name)
            sourceSize: Qt.size(Math.ceil(icon.size * icon.zoom), Math.ceil(icon.size * icon.zoom))
            smooth: true
            color: icon.color
            fillMode: Image.PreserveAspectFit
        }
    }
}
