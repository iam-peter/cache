import QtQuick
import QtQuick.Controls
import IgittStyle as Styles
import NoteApp

// A file attached to a note: its name, type and size; it opens with the
// application the system has for it.
Rectangle {
    id: block

    required property var segment

    implicitHeight: row.implicitHeight + 2 * Styles.Primitives.spacing.sp075
    radius: Theme.radius
    color: Theme.s.background_secondary
    border.color: Theme.s.stroke_subtle

    Row {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: Styles.Primitives.spacing.sp100
        spacing: Styles.Primitives.spacing.sp100

        Rectangle { // the file type, as a badge
            anchors.verticalCenter: parent.verticalCenter
            width: 36
            height: 40
            radius: Theme.radius
            color: Styles.Primitives.alpha(Theme.accent, 0.15)
            Label {
                anchors.centerIn: parent
                width: parent.width - 4
                horizontalAlignment: Text.AlignHCenter
                text: block.segment.extension || "FILE"
                elide: Text.ElideRight
                font.pixelSize: 10
                font.weight: Styles.Primitives.fontWeight.bold
                color: Theme.accent
            }
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 36 - 2 * 28 - 3 * parent.spacing
            Label {
                width: parent.width
                text: block.segment.name
                elide: Text.ElideMiddle
                font.weight: Styles.Primitives.fontWeight.medium
            }
            Label {
                text: block.segment.size
                color: Theme.textMuted
                font.pixelSize: Styles.Primitives.fontSize.small
            }
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            iconName: "external-link"
            onClicked: Qt.openUrlExternally(block.segment.url)
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Open")
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            iconName: "folder-open"
            onClicked: Qt.openUrlExternally(block.segment.folder)
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Show in folder")
        }
    }
}
