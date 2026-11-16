import QtQuick
import Cache

// While a picture is exported without its text (Theme.redacting): covers
// the text it is put into, in `cover`, the colour behind that text, and
// draws a grey bar for each of its lines. The layout shows, not what it
// says. Put it into a Text or Label as a child.
Item {
    id: bars

    property color cover: Theme.canvas
    readonly property Item target: parent

    anchors.fill: parent
    visible: Theme.redacting
    z: 1000

    Rectangle {
        anchors.fill: parent
        color: bars.cover
    }

    // Rich text doesn't count its lines: as many as its height holds
    readonly property int count: bars.target.textFormat === Text.RichText
        ? Math.max(1, Math.round(Math.min(bars.target.contentHeight, bars.height)
                                 / (Math.max(8, bars.target.font.pixelSize) * 1.45)))
        : Math.max(1, bars.target.lineCount || 1)

    Repeater {
        model: bars.visible ? bars.count : 0
        delegate: Rectangle {
            required property int index
            readonly property int count: bars.count
            readonly property real block: Math.min(bars.target.contentHeight || bars.height, bars.height)
            readonly property real line: block / count
            readonly property real full: Math.min(bars.target.contentWidth || bars.width, bars.width)
            // Lines of slightly different lengths, the last a short one
            width: full * (count > 1 && index === count - 1 ? 0.55 : 0.78 + 0.22 * ((index * 5 + 3) % 7) / 6)
            height: Math.max(2, line * 0.55)
            radius: height / 2
            x: bars.target.horizontalAlignment === Text.AlignHCenter ? (bars.width - width) / 2
             : bars.target.horizontalAlignment === Text.AlignRight ? bars.width - width : 0
            y: (bars.target.verticalAlignment === Text.AlignVCenter ? (bars.height - block) / 2
              : bars.target.verticalAlignment === Text.AlignBottom ? bars.height - block : 0)
               + index * line + (line - height) / 2
            color: Qt.alpha(Theme.text, 0.22)
        }
    }
}
