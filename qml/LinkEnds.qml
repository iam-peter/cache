import QtQuick
import QtQuick.Controls
import Cache

// The two ends of the selected connector. Drag one onto another side of
// its item, or onto another item (its own for a loop), to attach it there.
Item {
    id: ends

    required property Item canvas
    readonly property Item connector: canvas.selectedLinkId
                                      ? (canvas.cardsVersion, Board.links, canvas.connectorFor(canvas.selectedLinkId)) : null
    readonly property var geometry: connector ? connector.geometry : null

    // Stays visible while an end is dragged: hiding it would end the drag
    visible: geometry !== null && !canvas.exporting

    Repeater {
        model: ends.geometry ? [["from", ends.geometry.start], ["to", ends.geometry.end]] : []
        delegate: Rectangle {
            id: end
            required property var modelData
            readonly property real inv: 1 / ends.canvas.zoom
            width: 12 * inv
            height: width
            radius: width / 2
            x: modelData[1].x - width / 2
            y: modelData[1].y - height / 2
            color: Theme.s.background_elevated
            border.color: Theme.accent
            border.width: 2.5 * inv

            MouseArea {
                anchors.fill: parent
                anchors.margins: -5 * end.inv
                cursorShape: Qt.CrossCursor
                preventStealing: true
                hoverEnabled: true
                onPressed: (mouse) => ends.canvas.beginRelink(ends.canvas.selectedLinkId, end.modelData[0],
                                                              mapToItem(ends.parent, mouse.x, mouse.y))
                onPositionChanged: (mouse) => {
                    if (pressed)
                        ends.canvas.updateLink(mapToItem(ends.parent, mouse.x, mouse.y))
                }
                onReleased: (mouse) => ends.canvas.endLink(mapToItem(ends.parent, mouse.x, mouse.y))
                ToolTip.visible: containsMouse && !pressed
                ToolTip.delay: 500
                ToolTip.text: qsTr("Drag onto a side of an item to attach this end there")
            }
        }
    }
}
