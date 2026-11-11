import QtQuick
import QtQuick.Controls
import IgittStyle as Styles
import NoteApp

// One note on the canvas: frame, move/resize behaviour and the floating
// toolbar. What is inside, text and media, is NoteBody's.
Item {
    id: root

    required property int index
    required property string itemId
    required property real posX
    required property real posY
    required property real itemWidth
    required property real itemHeight
    required property int itemZ
    required property string itemColor
    required property string itemText
    required property Item canvas

    readonly property bool selected: canvas.selectedId === itemId
    readonly property bool editing: canvas.editingId === itemId
    readonly property real inv: 1 / canvas.zoom
    readonly property real minW: 160
    readonly property real minH: 70
    readonly property color fillColor: Theme.noteColor(itemColor)

    x: posX
    y: posY
    width: itemWidth
    height: itemHeight
    z: itemZ

    // Dragging and resizing write to x/y/width/height directly; once done,
    // store the result in the model and bind to it again.
    function commitGeometry() {
        Board.setGeometry(index, x, y, width, height)
        x = Qt.binding(() => posX)
        y = Qt.binding(() => posY)
        width = Qt.binding(() => itemWidth)
        height = Qt.binding(() => itemHeight)
    }

    Styles.Shadow {
        anchors.fill: frame
        radius: frame.radius
        depth: root.selected ? 3 : 1
    }

    Rectangle {
        id: frame
        anchors.fill: parent
        radius: Theme.radius
        color: root.fillColor
        border.width: (root.selected ? 2 : 1) * root.inv
        border.color: root.selected ? Theme.accent : Theme.cardBorder
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        enabled: !root.editing
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        drag.target: pressedButtons & Qt.LeftButton ? root : null
        drag.threshold: 2
        drag.smoothed: false
        preventStealing: true
        cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.ArrowCursor

        onPressed: (mouse) => {
            root.canvas.select(root.itemId)
            Board.raise(root.index)
            root.canvas.forceActiveFocus()
            if (mouse.button === Qt.RightButton)
                root.canvas.openCardMenu(root.index)
        }
        onReleased: {
            if (root.x !== root.posX || root.y !== root.posY)
                root.commitGeometry()
        }
        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton)
                root.canvas.startEditing(root.itemId)
        }
    }

    NoteBody {
        anchors.fill: parent
        card: root
    }

    // Floating toolbar. Scaled by 1/zoom so it keeps its screen size.
    Rectangle {
        id: toolbar
        visible: root.selected && !dragArea.drag.active
        scale: root.inv
        transformOrigin: Item.BottomLeft
        x: 0
        y: -height - 8 * root.inv
        z: 10
        width: actions.implicitWidth + 2 * Styles.Primitives.spacing.sp050
        height: actions.implicitHeight + 2 * Styles.Primitives.spacing.sp050
        radius: Theme.radius
        color: Theme.s.background_elevated
        border.color: Theme.s.stroke_subtle

        Styles.Shadow {
            z: -1
            anchors.fill: parent
            radius: parent.radius
            depth: 2
        }

        Row {
            id: actions
            anchors.centerIn: parent
            spacing: Styles.Primitives.spacing.sp025

            // Move grip: works while editing too
            IconButton {
                iconName: "grip-vertical"
                ToolTip.visible: hovered && !grip.pressed
                ToolTip.delay: 600
                ToolTip.text: qsTr("Drag to move")
                MouseArea {
                    id: grip
                    anchors.fill: parent
                    cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    drag.target: root
                    drag.threshold: 1
                    drag.smoothed: false
                    preventStealing: true
                    onReleased: root.commitGeometry()
                }
            }

            Repeater {
                model: Theme.noteKeys
                delegate: Item {
                    id: swatch
                    required property string modelData
                    width: 20
                    height: 24
                    Rectangle {
                        anchors.centerIn: parent
                        width: 14
                        height: 14
                        radius: 7
                        color: swatch.modelData === "" ? Theme.card : Theme.swatch(swatch.modelData)
                        border.width: root.itemColor === swatch.modelData ? 2 : 1
                        border.color: root.itemColor === swatch.modelData ? Theme.s.stroke_focus : Theme.s.stroke_field
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Board.setColor(root.index, swatch.modelData)
                    }
                }
            }

            ToolSeparator { height: 24 }

            IconButton {
                iconName: root.editing ? "check" : "pencil"
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: root.editing ? qsTr("Done (Esc)") : qsTr("Edit (Enter)")
                onClicked: root.editing ? root.canvas.finishEditing() : root.canvas.startEditing(root.itemId)
            }
            IconButton {
                iconName: "image"
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Add images or videos to this note")
                onClicked: root.canvas.pickMedia(root.index)
            }
            IconButton {
                iconName: "copy"
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Copy with images, to paste into Teams or mail (Ctrl+C)")
                onClicked: root.canvas.copyRow(root.index)
            }
            IconButton {
                iconName: "trash-2"
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Delete (Del)")
                onClicked: {
                    const row = root.index
                    root.canvas.clearSelection()
                    Board.remove(row)
                }
            }
        }
    }

    // Resize handle
    Rectangle {
        id: handle
        visible: root.selected
        z: 10
        width: 10 * root.inv
        height: 10 * root.inv
        radius: 2 * root.inv
        x: root.width - width / 2
        y: root.height - height / 2
        color: Theme.accent
        border.color: Theme.s.background_elevated
        border.width: 1.5 * root.inv

        MouseArea {
            anchors.fill: parent
            anchors.margins: -6 * root.inv
            cursorShape: Qt.SizeFDiagCursor
            preventStealing: true

            property point start
            property real startW
            property real startH

            onPressed: (mouse) => {
                start = mapToItem(root.parent, mouse.x, mouse.y)
                startW = root.width
                startH = root.height
            }
            onPositionChanged: (mouse) => {
                const p = mapToItem(root.parent, mouse.x, mouse.y)
                root.width = Math.max(root.minW, startW + p.x - start.x)
                root.height = Math.max(root.minH, startH + p.y - start.y)
            }
            onReleased: root.commitGeometry()
        }
    }
}
