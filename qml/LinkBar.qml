import QtQuick
import QtQuick.Controls
import IgittStyle as Styles
import NoteApp

// At the middle of the selected connector: its route, arrows, line, colour
// and label.
Rectangle {
    id: bar

    required property Item canvas
    readonly property var link: {
        canvas.selectedLinkId
        Board.links
        return Board.links.find(l => l.id === canvas.selectedLinkId) || null
    }
    readonly property Item connector: link ? (canvas.cardsVersion, canvas.connectorFor(link.id)) : null
    readonly property point mid: connector && connector.geometry ? connector.geometry.mid : Qt.point(0, 0)

    function editLabel() {
        if (!link)
            return
        labelField.text = link.label
        labelField.visible = true
        labelField.forceActiveFocus()
        labelField.selectAll()
    }

    visible: link !== null && connector !== null && !canvas.exporting
    scale: 1 / canvas.zoom
    transformOrigin: Item.Top
    x: mid.x - width / 2
    y: mid.y + 14 / canvas.zoom
    width: column.implicitWidth + 2 * Styles.Primitives.spacing.sp050
    height: column.implicitHeight + 2 * Styles.Primitives.spacing.sp050
    radius: Theme.radius
    color: Theme.s.background_elevated
    border.color: Theme.s.stroke_subtle
    onLinkChanged: if (!link) labelField.visible = false

    Styles.Shadow {
        z: -1
        anchors.fill: parent
        radius: parent.radius
        depth: 2
    }

    component Choice: IconButton {
        property string key
        property string value
        property string tip
        checkable: true
        checked: bar.link && bar.link[key] === value
        onClicked: Board.setLink(bar.link.id, { [key]: value })
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.text: tip
    }

    Column {
        id: column
        anchors.centerIn: parent
        spacing: Styles.Primitives.spacing.sp050

        Row {
            spacing: Styles.Primitives.spacing.sp025
            Choice { key: "style"; value: "curve"; iconName: "spline"; tip: qsTr("Curved") }
            Choice { key: "style"; value: "straight"; iconName: "minus"; tip: qsTr("Straight") }
            Choice { key: "style"; value: "elbow"; iconName: "corner-down-right"; tip: qsTr("Elbow") }
            ToolSeparator { height: 24 }
            Choice { key: "arrow"; value: "end"; iconName: "arrow-right"; tip: qsTr("Arrow at the end") }
            Choice { key: "arrow"; value: "both"; iconName: "arrow-left-right"; tip: qsTr("Arrows at both ends") }
            Choice { key: "arrow"; value: "none"; iconName: "minus"; tip: qsTr("No arrows") }
            IconButton {
                iconName: "dashed-line"
                checkable: true
                checked: bar.link && bar.link.dashed
                onClicked: Board.setLink(bar.link.id, { dashed: checked })
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Dashed")
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
                        color: Theme.lineColor(parent.modelData)
                        border.width: bar.link && bar.link.color === parent.modelData ? 2 : 1
                        border.color: bar.link && bar.link.color === parent.modelData ? Theme.accent : Theme.s.stroke_field
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Board.setLink(bar.link.id, { color: parent.modelData })
                    }
                }
            }
            ToolSeparator { height: 24 }
            IconButton {
                iconName: "type"
                onClicked: bar.editLabel()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Label (double-click the connector)")
            }
            IconButton {
                iconName: "trash-2"
                onClicked: bar.canvas.deleteSelected()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Delete (Del)")
            }
        }

        TextField {
            id: labelField
            visible: false
            width: parent.width
            placeholderText: qsTr("Label")
            onTextEdited: Board.setLink(bar.link.id, { label: text })
            Keys.onReturnPressed: { visible = false; bar.canvas.forceActiveFocus() }
            Keys.onEscapePressed: { visible = false; bar.canvas.forceActiveFocus() }
        }
    }
}
