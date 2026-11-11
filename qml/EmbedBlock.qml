import QtQuick
import QtQuick.Controls
import IgittStyle as Styles
import Cache

// A [[link]] on a line of its own: the linked note's title, and while
// expanded (written ![[…]]) a preview of its content. The chevron hides
// and shows the preview by changing how the link is written.
Rectangle {
    id: block

    required property var segment
    required property Item card
    // The linked note, from any project
    readonly property var note: (Search.revision, Search.findNote(segment.target))
    readonly property bool expanded: segment.expanded && note.found

    implicitHeight: column.implicitHeight + 2 * Styles.Primitives.spacing.sp075
    radius: Theme.radius
    color: Styles.Primitives.alpha(Theme.link, 0.06)
    border.color: Styles.Primitives.alpha(Theme.link, 0.35)

    Rectangle { // accent along the left edge
        width: 3
        height: parent.height
        radius: 1.5
        color: block.note.found ? Theme.link : Theme.s.danger_default
    }

    Column {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Styles.Primitives.spacing.sp075
        anchors.leftMargin: Styles.Primitives.spacing.sp150
        spacing: Styles.Primitives.spacing.sp050

        Row {
            width: parent.width
            spacing: Styles.Primitives.spacing.sp050

            IconButton {
                visible: block.note.found
                iconName: block.segment.expanded ? "chevron-down" : "chevron-right"
                ToolTip.visible: hovered
                ToolTip.text: block.segment.expanded ? qsTr("Hide the preview") : qsTr("Show a preview")
                onClicked: Board.setEmbed(block.card.index, block.segment.line, !block.segment.expanded)
            }
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "link"
                size: 14
                color: block.note.found ? Theme.link : Theme.s.danger_default
            }
            Label {
                id: title
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 80
                elide: Text.ElideRight
                text: block.note.found
                      ? (block.segment.alias || block.note.title)
                        + (block.note.current ? "" : "  ·  " + block.note.projectName)
                      : qsTr("%1 (no such note yet)").arg(block.segment.target)
                font.weight: Styles.Primitives.fontWeight.medium
                color: block.note.found ? Theme.link : Theme.textMuted
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: block.card.canvas.openWikiLink(block.segment.target, block.card.itemId)
                }
            }
        }

        // The preview: the linked note's text, small, cut off after a while
        Item {
            visible: block.expanded
            width: parent.width
            height: visible ? Math.min(preview.implicitHeight, 240) : 0
            clip: true

            Text {
                id: preview
                width: parent.width
                textFormat: Text.RichText
                // See NoteBody.qml
                renderType: Text.CurveRendering
                wrapMode: Text.Wrap
                color: Theme.text
                font.pixelSize: 12
                text: {
                    if (!block.expanded)
                        return ""
                    // Text and code only: no players or previews inside previews
                    const parts = Board.renderSegments(block.note.text, Math.max(40, width - 2), 12, Theme.renderColors)
                    return parts.filter(p => p.kind === "text" || p.kind === "code").map(p => p.html).join("")
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 24
                visible: preview.implicitHeight > 240
                gradient: Gradient {
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 1; color: block.card.fillColor }
                }
            }
        }
    }
}
