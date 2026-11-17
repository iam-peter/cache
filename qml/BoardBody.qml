import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import IgittStyle as Styles
import Cache

// Another project's canvas in miniature, as Obsidian nests canvases: its
// name, how many notes it has and where its items lie. Double-click (or
// the button) opens it.
Item {
    id: body

    required property Item card
    readonly property string project: card.itemText
    // Read again when this project's canvas is (the other one may have
    // changed since)
    readonly property var preview: Board.projectPath !== "" ? Board.boardPreview(project) : ({})
    readonly property rect bounds: preview.bounds || Qt.rect(0, 0, 0, 0)

    function open() {
        const row = Workspace.projects().findIndex(p => p.name === body.project)
        if (row >= 0)
            Workspace.currentIndex = row
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Styles.Primitives.spacing.sp100
        spacing: Styles.Primitives.spacing.sp075

        RowLayout {
            Layout.fillWidth: true
            spacing: Styles.Primitives.spacing.sp075
            Icon {
                zoom: Theme.iconZoom // crisp however far the canvas zooms
                name: "folder"
                size: 14
                color: Theme.textMuted
            }
            Label {
                Layout.fillWidth: true
                text: body.project
                font.weight: Styles.Primitives.fontWeight.semiBold
                elide: Text.ElideRight
                Redacted { cover: body.card.fillColor }
            }
            Label {
                text: !body.preview.found ? qsTr("not found")
                      : body.preview.notes === 1 ? qsTr("1 note") : qsTr("%1 notes").arg(body.preview.notes || 0)
                color: body.preview.found ? Theme.textMuted : Theme.s.danger_default
                font.pixelSize: Styles.Primitives.fontSize.small
            }
            IconButton {
                zoom: Theme.iconZoom // crisp however far the canvas zooms
                visible: body.preview.found === true
                iconName: "folder-open"
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Open this project (or double-click)")
                onClicked: body.open()
            }
        }

        // The canvas, scaled to fit
        Rectangle {
            id: mini
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: Theme.canvas
            border.color: Theme.cardBorder
            clip: true

            readonly property real pad: 8
            readonly property real factor: body.bounds.width > 0 && body.bounds.height > 0
                ? Math.min((width - 2 * pad) / body.bounds.width, (height - 2 * pad) / body.bounds.height) : 1
            readonly property real offsetX: (width - body.bounds.width * factor) / 2 - body.bounds.x * factor
            readonly property real offsetY: (height - body.bounds.height * factor) / 2 - body.bounds.y * factor

            Repeater {
                model: body.preview.items || []
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool section: modelData.type === "section"
                    readonly property bool stroke: modelData.type === "stroke"
                    x: mini.offsetX + modelData.x * mini.factor
                    y: mini.offsetY + modelData.y * mini.factor
                    z: section ? 0 : 1 // sections under the rest, above the canvas
                    width: Math.max(2, modelData.w * mini.factor)
                    height: Math.max(2, modelData.h * mini.factor)
                    radius: Math.min(3, width / 4)
                    color: section ? Theme.sectionColor(modelData.color)
                         : stroke ? "transparent"
                         : modelData.type === "shape" ? Theme.shapeColor(modelData.color)
                         : modelData.type === "text" ? "transparent"
                         : Theme.noteColor(modelData.color)
                    border.color: stroke ? Qt.alpha(Theme.text, 0.25) : Theme.cardBorder
                    border.width: modelData.type === "text" ? 0 : 1
                    // Titles where there is room for them
                    Label {
                        anchors.fill: parent
                        anchors.margins: 3
                        visible: parent.height > 14 && parent.width > 30 && text !== ""
                        text: parent.modelData.title
                        font.pixelSize: Math.min(11, Math.max(8, parent.height / 3))
                        elide: Text.ElideRight
                        wrapMode: Text.Wrap
                        maximumLineCount: Math.max(1, Math.floor(parent.height / 14))
                        color: Theme.text
                        Redacted { cover: Theme.noteColor(parent.parent.modelData.color) }
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: (body.preview.items || []).length === 0
                text: body.preview.found ? qsTr("Nothing here yet") : qsTr("No project “%1” in this workspace").arg(body.project)
                color: Theme.textMuted
                font.pixelSize: Styles.Primitives.fontSize.small
            }
        }
    }
}
