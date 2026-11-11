import QtQuick
import QtQuick.Controls
import IgittStyle as Styles
import Cache

// Above several selected items: aligning, spacing, colour, copying and
// exporting them. In canvas coordinates, scaled to keep its screen size.
Rectangle {
    id: bar

    required property Item canvas
    readonly property var ids: canvas.selection
    // Where the selection is; refreshed when it or the board changes
    property rect bounds: Qt.rect(0, 0, 0, 0)
    function refresh() { bounds = ids.length > 1 ? Board.boundsOf(ids) : Qt.rect(0, 0, 0, 0) }
    onIdsChanged: refresh()
    Connections {
        target: Board
        function onDataChanged() { bar.refresh() }
    }
    Connections {
        target: bar.canvas
        function onMovingChanged() { if (!bar.canvas.moving) bar.refresh() }
    }

    visible: ids.length > 1 && !canvas.moving && !canvas.exporting
    scale: 1 / canvas.zoom
    transformOrigin: Item.BottomLeft
    x: bounds.x
    y: bounds.y - height - 12 / canvas.zoom
    width: row.implicitWidth + 2 * Styles.Primitives.spacing.sp050
    height: row.implicitHeight + 2 * Styles.Primitives.spacing.sp050
    radius: Theme.radius
    color: Theme.s.background_elevated
    border.color: Theme.s.stroke_subtle

    Styles.Shadow {
        z: -1
        anchors.fill: parent
        radius: parent.radius
        depth: 2
    }

    component Action: IconButton {
        property string tip
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.text: tip
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Styles.Primitives.spacing.sp025

        Label {
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 6
            rightPadding: 6
            text: qsTr("%1 selected").arg(bar.ids.length)
            color: Theme.textMuted
            font.pixelSize: Styles.Primitives.fontSize.small
        }
        ToolSeparator { height: 24 }
        Action { iconName: "align-left"; tip: qsTr("Align left edges"); onClicked: Board.align(bar.ids, "left") }
        Action { iconName: "align-hcenter"; tip: qsTr("Align centres"); onClicked: Board.align(bar.ids, "hcenter") }
        Action { iconName: "align-right"; tip: qsTr("Align right edges"); onClicked: Board.align(bar.ids, "right") }
        Action { iconName: "align-top"; tip: qsTr("Align tops"); onClicked: Board.align(bar.ids, "top") }
        Action { iconName: "align-vcenter"; tip: qsTr("Align middles"); onClicked: Board.align(bar.ids, "vcenter") }
        Action { iconName: "align-bottom"; tip: qsTr("Align bottoms"); onClicked: Board.align(bar.ids, "bottom") }
        Action {
            iconName: "distribute-h"
            enabled: bar.ids.length > 2
            tip: qsTr("Space evenly side by side")
            onClicked: Board.distribute(bar.ids, true)
        }
        Action {
            iconName: "distribute-v"
            enabled: bar.ids.length > 2
            tip: qsTr("Space evenly one above the other")
            onClicked: Board.distribute(bar.ids, false)
        }
        ToolSeparator { height: 24 }
        Repeater {
            model: Theme.noteKeys
            delegate: Item {
                required property string modelData
                width: 20
                height: 24
                Rectangle {
                    anchors.centerIn: parent
                    width: 14
                    height: 14
                    radius: 7
                    color: parent.modelData === "" ? Theme.card : Theme.swatch(parent.modelData)
                    border.color: Theme.s.stroke_field
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Board.setColorOf(bar.ids, parent.modelData)
                }
            }
        }
        ToolSeparator { height: 24 }
        Action { iconName: "copy"; tip: qsTr("Copy (Ctrl+C)"); onClicked: bar.canvas.copySelected() }
        Action { iconName: "image"; tip: qsTr("Copy as a picture, for Teams (Ctrl+Shift+C)"); onClicked: bar.canvas.exportImage(bar.ids, "copy") }
        Action { iconName: "layout-template"; tip: qsTr("Save as template"); onClicked: bar.canvas.saveTemplate() }
        Action { iconName: "trash-2"; tip: qsTr("Delete (Del)"); onClicked: bar.canvas.deleteSelected() }
    }
}
