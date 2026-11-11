import QtQuick
import QtQuick.Controls
import IgittStyle as Styles
import Cache

// A fenced code block of a note, syntax-highlighted, with a button that
// copies the code.
Rectangle {
    id: block

    required property var segment

    implicitHeight: column.implicitHeight + 2 * Styles.Primitives.spacing.sp100
    radius: Theme.radius
    color: Theme.codeBackground
    border.color: Theme.s.stroke_subtle

    Column {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Styles.Primitives.spacing.sp100
        spacing: Styles.Primitives.spacing.sp050

        Item {
            width: parent.width
            height: Math.max(language.implicitHeight, copy.implicitHeight)
            Label {
                id: language
                anchors.verticalCenter: parent.verticalCenter
                text: block.segment.language || qsTr("code")
                color: Theme.textMuted
                font.pixelSize: Styles.Primitives.fontSize.small
            }
            IconButton {
                id: copy
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                iconName: done.running ? "check" : "copy"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Copy the code")
                onClicked: {
                    Board.copyText(block.segment.code)
                    done.restart()
                }
                Timer { id: done; interval: 1500 }
            }
        }

        Text {
            width: parent.width
            text: block.segment.html
            textFormat: Text.RichText
            wrapMode: Text.Wrap
            color: Theme.text
            font.family: Theme.monoFont.family
            font.pixelSize: 13
        }
    }
}
