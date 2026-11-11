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

    implicitWidth: size
    implicitHeight: size

    IconImage {
        anchors.fill: parent
        source: Icons.source(icon.name)
        sourceSize: Qt.size(icon.size, icon.size)
        color: icon.color
        fillMode: Image.PreserveAspectFit
    }
}
