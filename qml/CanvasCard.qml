import QtQuick
import QtQuick.Controls
import IgittStyle as Styles
import Cache

// One item on the canvas, any type: selection, moving (with the rest of the
// selection), resizing, connection handles, the folding badge of mindmap
// branches and a floating toolbar. What it shows is its body's.
Item {
    id: root

    required property int index
    required property string itemId
    required property string itemType
    required property real posX
    required property real posY
    required property real itemWidth
    required property real itemHeight
    required property int itemZ
    required property string itemColor
    required property string itemText
    required property string itemShape
    required property var itemPoints
    required property real itemSize
    required property bool itemCollapsed
    required property string itemTask // a note that is a task: "", "open" or "done"
    required property bool itemHidden
    required property int descendants
    required property int childCount
    required property Item canvas

    readonly property bool isNote: itemType === "note"
    readonly property bool isShape: itemType === "shape"
    readonly property bool isText: itemType === "text"
    readonly property bool isSection: itemType === "section"
    readonly property bool isStroke: itemType === "stroke"
    readonly property bool isLink: itemType === "link"
    // On a card of its own: notes and web links
    readonly property bool framed: isNote || isLink
    readonly property bool connectable: isNote || isShape || isText || isLink

    // Pictures of the canvas show no selection
    readonly property bool selected: !canvas.exporting && canvas.selection.indexOf(itemId) >= 0
    readonly property bool soleSelection: selected && canvas.selection.length === 1
    readonly property bool editing: canvas.editingId === itemId
    // A note being edited: its toolbar holds the Markdown buttons
    readonly property bool formatting: editing && isNote
    readonly property bool linkTarget: canvas.linkTargetId === itemId
    readonly property real inv: 1 / canvas.zoom
    readonly property real minW: isNote || isLink ? 160 : 30
    readonly property real minH: isNote ? 70 : isLink ? 60 : 20
    readonly property color fillColor: Theme.noteColor(itemColor)
    readonly property bool chrome: !canvas.exporting

    x: posX
    y: posY
    width: itemWidth
    height: itemHeight
    // Sections lie under everything else
    z: isSection ? itemZ - 1000000 : itemZ
    visible: !itemHidden && (!canvas.exporting || canvas.exportSet[itemId] === true)

    Component.onCompleted: canvas.registerCard(itemId, root)
    Component.onDestruction: canvas.unregisterCard(itemId, root)

    // Moving and resizing write to x/y/width/height directly; once done the
    // model has the result and the card binds to it again
    function rebind() {
        x = Qt.binding(() => posX)
        y = Qt.binding(() => posY)
        width = Qt.binding(() => itemWidth)
        height = Qt.binding(() => itemHeight)
    }
    // A note as tall as what it holds
    function fitToContent() {
        if (isNote && body.item)
            body.item.fitToContent()
    }
    function commitGeometry() {
        Board.setGeometry(index, x, y, width, height)
        rebind()
    }

    // Pointer handling shared by the body, the grip and the section title
    property bool linking: false
    property point lastPoint
    function pressAt(p, modifiers) {
        lastPoint = p
        canvas.forceActiveFocus()
        if (canvas.tool === "connector" && connectable) {
            linking = true
            canvas.beginLink(itemId, p)
            return
        }
        linking = false
        if (modifiers & (Qt.ShiftModifier | Qt.ControlModifier))
            canvas.toggleSelected(itemId)
        else if (!selected)
            canvas.select(itemId)
        if (!isSection)
            Board.raise(index)
        if (selected)
            canvas.beginMove(itemId, p)
    }
    function dragTo(p, modifiers) {
        lastPoint = p
        if (linking)
            canvas.updateLink(p)
        else
            canvas.updateMove(p, modifiers)
    }
    function release() {
        if (linking)
            canvas.endLink(lastPoint)
        else
            canvas.endMove()
        linking = false
    }

    // Note and link card: shadow and frame
    Styles.Shadow {
        visible: root.framed
        anchors.fill: parent
        radius: Theme.radius
        depth: root.selected ? 3 : 1
    }
    Rectangle {
        visible: root.framed
        anchors.fill: parent
        radius: Theme.radius
        color: root.fillColor
        border.width: (root.selected || root.linkTarget ? 2 : 1) * root.inv
        border.color: root.selected || root.linkTarget ? Theme.accent : Theme.cardBorder
    }
    // Others show their selection as an outline
    Rectangle {
        visible: root.chrome && ((root.selected && (root.isText || root.isStroke)) || (root.linkTarget && !root.framed))
        anchors.fill: parent
        anchors.margins: -4 * root.inv
        radius: Theme.radius
        color: "transparent"
        border.width: 1.5 * root.inv
        border.color: Theme.accent
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        // Other tools draw or create on top of items: the canvas gets the press
        // Space while moving turns snapping off; only before it pans
        enabled: pressed || (!root.editing && !root.canvas.spaceHeld
                 && (root.canvas.tool === "select" || root.canvas.tool === "connector"))
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true
        cursorShape: root.canvas.tool === "connector" && root.connectable ? Qt.CrossCursor
                     : pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
        onPressed: (mouse) => {
            // A thin line takes clicks only on itself; beside it they go on
            // to what lies underneath
            if (root.isStroke && body.item && !body.item.hit(Qt.point(mouse.x, mouse.y))) {
                mouse.accepted = false
                return
            }
            if (mouse.button === Qt.RightButton) {
                if (!root.selected)
                    root.canvas.select(root.itemId)
                root.canvas.openCardMenu(root.index)
                return
            }
            root.pressAt(mapToItem(root.parent, mouse.x, mouse.y), mouse.modifiers)
        }
        onPositionChanged: (mouse) => {
            if (pressedButtons & Qt.LeftButton)
                root.dragTo(mapToItem(root.parent, mouse.x, mouse.y), mouse.modifiers)
        }
        onReleased: (mouse) => {
            if (mouse.button === Qt.LeftButton)
                root.release()
        }
        // Taken away mid-drag (a popup, the window losing the mouse): the
        // move or connector still ends, or the canvas would stay moving
        onCanceled: root.release()
        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton && !root.isStroke)
                root.canvas.startEditing(root.itemId)
        }
    }

    Loader {
        id: body
        anchors.fill: parent
        // A note whose task is done: faded, its tick still clear
        opacity: root.isNote && root.itemTask === "done" && !root.editing ? 0.5 : 1
        sourceComponent: root.isNote ? noteBody : root.isShape ? shapeBody : root.isText ? textBody
                       : root.isSection ? sectionBody : root.isLink ? linkBody : strokeBody
    }

    // A note that is a task: its box to tick, in the corner
    Rectangle {
        visible: root.isNote && root.itemTask !== "" && !root.editing
        z: 5
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 10
        width: 20
        height: 20
        radius: 4
        color: root.itemTask === "done" ? Theme.accent : Theme.s.background_elevated
        border.color: root.itemTask === "done" ? Theme.accent : Theme.s.stroke_field
        border.width: 1.5
        Icon {
            anchors.centerIn: parent
            visible: root.itemTask === "done"
            name: "check"
            size: 14
            color: Theme.s.text_on_primary
        }
        MouseArea {
            anchors.fill: parent
            anchors.margins: -4
            cursorShape: Qt.PointingHandCursor
            onClicked: Board.setNoteTask(root.index, root.itemTask === "done" ? "open" : "done")
            ToolTip.visible: containsMouse
            ToolTip.delay: 600
            ToolTip.text: root.itemTask === "done" ? qsTr("Done; click to open it again") : qsTr("Mark as done")
            hoverEnabled: true
        }
    }
    Component { id: noteBody; NoteBody { card: root } }
    Component { id: shapeBody; ShapeBody { card: root } }
    Component { id: textBody; TextBody { card: root } }
    Component { id: sectionBody; SectionBody { card: root } }
    Component { id: strokeBody; StrokeBody { card: root } }
    Component { id: linkBody; LinkBody { card: root } }

    // --- Mindmap: folding a branch ---------------------------------------
    HoverHandler { id: cardHover }
    Rectangle {
        // Only where it is wanted: a folded branch, or the item in hand
        visible: root.chrome && root.childCount > 0 && root.connectable
                 && (root.itemCollapsed || root.selected || cardHover.hovered || foldHover.hovered)
        z: 11 // over the side's resize handle
        scale: root.inv
        x: root.width + 4 * root.inv - width / 2
        y: root.height / 2 - height / 2
        width: Math.max(20, foldLabel.implicitWidth + 10)
        height: 20
        radius: 10
        color: root.itemCollapsed ? Theme.accent : Theme.s.background_elevated
        border.color: root.itemCollapsed ? Theme.accent : Theme.s.stroke_field
        Label {
            id: foldLabel
            anchors.centerIn: parent
            text: root.itemCollapsed ? "+" + root.descendants : "−"
            font.pixelSize: 12
            font.weight: Styles.Primitives.fontWeight.semiBold
            color: root.itemCollapsed ? Styles.TokenInterface.semantics.text_on_primary : Theme.textMuted
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: Board.toggleCollapsed(root.itemId)
        }
        ToolTip.visible: foldHover.hovered
        ToolTip.text: root.itemCollapsed ? qsTr("Show the branch") : qsTr("Fold the branch")
        HoverHandler { id: foldHover }
    }

    // --- Notes linking here ------------------------------------------------
    Rectangle {
        id: backlinks
        readonly property int count: root.isNote && Search.revision >= 0 ? Search.backlinkCount(Board.projectPath, root.itemId) : 0
        visible: root.chrome && count > 0
        scale: root.inv
        transformOrigin: Item.TopLeft
        x: 0
        y: root.height + 4 * root.inv
        width: backlinksRow.implicitWidth + 12
        height: 22
        radius: Theme.radius
        color: Styles.Primitives.alpha(Theme.link, 0.12)
        Row {
            id: backlinksRow
            anchors.centerIn: parent
            spacing: 4
            Icon { anchors.verticalCenter: parent.verticalCenter; name: "corner-up-left"; size: 12; color: Theme.link }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: backlinks.count === 1 ? qsTr("1 note links here") : qsTr("%1 notes link here").arg(backlinks.count)
                font.pixelSize: 11
                color: Theme.link
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.canvas.showBacklinks(root.itemId, backlinks)
        }
    }

    // --- Connection handles: drag one out to connect ------------------------
    Repeater {
        model: root.chrome && root.soleSelection && root.connectable && !root.editing
               && root.canvas.tool === "select" ? 4 : 0
        delegate: Rectangle {
            required property int index
            readonly property real gap: 14 * root.inv
            width: 12 * root.inv
            height: width
            radius: width / 2
            x: index === 0 ? root.width / 2 - width / 2 : index === 1 ? root.width + gap - width / 2
               : index === 2 ? root.width / 2 - width / 2 : -gap - width / 2
            y: index === 0 ? -gap - height / 2 : index === 1 ? root.height / 2 - height / 2
               : index === 2 ? root.height + gap - height / 2 : root.height / 2 - height / 2
            color: Theme.s.background_elevated
            border.color: Theme.accent
            border.width: 2 * root.inv
            z: 11
            MouseArea {
                anchors.fill: parent
                anchors.margins: -4 * root.inv
                cursorShape: Qt.CrossCursor
                preventStealing: true
                onPressed: (mouse) => {
                    root.linking = true
                    root.lastPoint = mapToItem(root.parent, mouse.x, mouse.y)
                    // Out of this side; where it goes in is up to where it is dropped
                    root.canvas.beginLink(root.itemId, root.lastPoint, ["top", "right", "bottom", "left"][parent.index])
                }
                onPositionChanged: (mouse) => root.dragTo(mapToItem(root.parent, mouse.x, mouse.y), mouse.modifiers)
                onReleased: root.release()
                onCanceled: root.release()
            }
        }
    }

    // --- While a connector is dragged onto this item: its sides --------------
    Repeater {
        model: root.linkTarget && root.connectable ? ["top", "right", "bottom", "left"] : []
        delegate: Rectangle {
            required property string modelData
            readonly property bool chosen: root.canvas.linkTargetSide === modelData
            width: (chosen ? 14 : 10) * root.inv
            height: width
            radius: width / 2
            z: 11
            x: modelData === "left" ? -width / 2 : modelData === "right" ? root.width - width / 2 : root.width / 2 - width / 2
            y: modelData === "top" ? -height / 2 : modelData === "bottom" ? root.height - height / 2 : root.height / 2 - height / 2
            color: chosen ? Theme.accent : Theme.s.background_elevated
            border.color: Theme.accent
            border.width: 2 * root.inv
        }
    }

    // Parts of the floating toolbar
    component Swatch: Item {
        id: swatch
        required property string key
        property color fill: Theme.swatch(key)
        width: 20
        height: 24
        Rectangle {
            anchors.centerIn: parent
            width: 14
            height: 14
            radius: 7
            color: swatch.key === "" ? Theme.card : swatch.fill
            border.width: root.itemColor === swatch.key ? 2 : 1
            border.color: root.itemColor === swatch.key ? Theme.accent : Theme.s.stroke_field
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: Board.setColorOf(root.canvas.selection, swatch.key)
        }
    }
    component Tip: IconButton {
        property string tip
        ToolTip.visible: hovered && tip.length > 0 && !root.canvas.moving
        ToolTip.delay: 600
        ToolTip.text: tip
    }

    // --- Floating toolbar, for one selected item ---------------------------
    // Built for the one selected item only: a toolbar in every card made
    // opening a project and adding a note slow
    Loader {
        active: root.chrome && root.soleSelection
        // Made between frames: the selection or the editor shows at once
        asynchronous: true
        z: 12
        sourceComponent: Component {
            Rectangle {
                id: toolbar
                // Out of sight while moving, but not hidden: hiding it would
                // take the mouse from its grip, ending the drag at once
                opacity: root.canvas.moving ? 0 : 1
                scale: root.inv
                transformOrigin: Item.BottomLeft
                x: 0
                y: -height - (root.isSection ? 40 : 10) * root.inv
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

                    Tip {
                        iconName: "grip-vertical"
                        tip: qsTr("Drag to move")
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            preventStealing: true
                            onPressed: (mouse) => root.pressAt(mapToItem(root.parent, mouse.x, mouse.y), 0)
                            onPositionChanged: (mouse) => root.dragTo(mapToItem(root.parent, mouse.x, mouse.y), mouse.modifiers)
                            onReleased: root.release()
                            onCanceled: root.release()
                        }
                    }

                // While editing a note: Markdown for the selection or its
                // lines, made between frames so the editor shows at once
                Loader {
                    active: root.formatting
                    asynchronous: true
                    // Gone from the row with its buttons, not an empty gap
                    visible: active && status === Loader.Ready
                    width: visible && item ? item.implicitWidth : 0
                    anchors.verticalCenter: parent.verticalCenter
                    sourceComponent: Row {
                        spacing: Styles.Primitives.spacing.sp025
                        Repeater {
                            model: [
                                ["h1", "heading-1", qsTr("Heading 1")], ["h2", "heading-2", qsTr("Heading 2")],
                                ["h3", "heading-3", qsTr("Heading 3")], [],
                                ["bold", "bold", qsTr("Bold (Ctrl+B)")], ["italic", "italic", qsTr("Italic (Ctrl+I)")],
                                ["strike", "strikethrough", qsTr("Strikethrough")], ["inline-code", "code", qsTr("Inline code (Ctrl+E)")],
                                ["link", "link", qsTr("Link (Ctrl+K)")], [],
                                ["bullet", "list", qsTr("Bulleted list")], ["number", "list-ordered", qsTr("Numbered list")],
                                ["task", "list-checks", qsTr("Checklist")], ["quote", "quote", qsTr("Quote")], [],
                                ["codeblock", "square-code", qsTr("Code block (Ctrl+Shift+K)")], ["table", "table", qsTr("Table")],
                                ["rule", "separator-horizontal", qsTr("Divider")]]
                            delegate: Loader {
                                required property var modelData
                                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                                sourceComponent: modelData.length === 0 ? formatGap : formatButton
                                Component {
                                    id: formatGap
                                    ToolSeparator { height: 24 }
                                }
                                Component {
                                    id: formatButton
                                    Tip {
                                        iconName: modelData[1]
                                        tip: modelData[2]
                                        // The editor keeps its focus and selection
                                        focusPolicy: Qt.NoFocus
                                        onClicked: root.canvas.formatRequested(root.itemId, modelData[0])
                                    }
                                }
                            }
                        }
                    }
                }

                // Shapes: which one
                    Repeater {
                        model: root.isShape ? [["rect", "square"], ["ellipse", "circle"], ["diamond", "diamond"], ["triangle", "triangle"]] : []
                        delegate: Tip {
                            required property var modelData
                            iconName: modelData[1]
                            checkable: true
                            checked: (root.itemShape || "rect") === modelData[0]
                            onClicked: Board.setShape(root.index, modelData[0])
                        }
                    }
                    // Text: how big
                    Repeater {
                        model: root.isText ? [["S", 16], ["M", 24], ["L", 36], ["XL", 56]] : []
                        delegate: ToolButton {
                            required property var modelData
                            styles: [Styles.ButtonStyle.small]
                            text: modelData[0]
                            checkable: true
                            checked: Math.round(root.itemSize) === modelData[1]
                            onClicked: {
                                Board.setSize(root.index, modelData[1])
                                Board.setGeometry(root.index, root.x, root.y, root.width, modelData[1] * 1.4 + 8)
                            }
                        }
                    }

                    Repeater {
                        model: root.formatting ? [] : root.isStroke || root.isText ? Theme.penColors : Theme.noteKeys
                        delegate: Swatch {
                            required property string modelData
                            key: modelData
                            fill: root.isStroke || root.isText ? Theme.penColor(modelData) : Theme.swatch(modelData)
                        }
                    }

                    ToolSeparator { height: 24; visible: !root.isStroke }

                    Tip {
                        visible: !root.isStroke
                        iconName: root.editing ? "check" : "pencil"
                        tip: root.editing ? qsTr("Done (Esc)") : root.isSection ? qsTr("Rename") : qsTr("Edit (Enter)")
                        onClicked: root.editing ? root.canvas.finishEditing() : root.canvas.startEditing(root.itemId)
                    }
                    Tip {
                        visible: root.isNote && !root.formatting
                        iconName: "square-check"
                        checkable: true
                        checked: root.itemTask !== ""
                        tip: root.itemTask !== "" ? qsTr("No longer a task") : qsTr("Make this note a task, with a box to tick")
                        onClicked: Board.setNoteTask(root.index, root.itemTask !== "" ? "" : "open")
                    }
                    Tip {
                        visible: root.isNote
                        iconName: "image"
                        tip: qsTr("Add images, videos or files to this note")
                        onClicked: root.canvas.pickMedia(root.index)
                    }
                    Tip {
                        visible: root.isNote && !root.formatting
                        iconName: "code"
                        tip: qsTr("Add a code block (Ctrl+Shift+K while editing)")
                        onClicked: root.canvas.insertCode(root.itemId)
                    }
                    Tip {
                        visible: root.isNote && !root.formatting
                        iconName: "copy"
                        tip: qsTr("Copy: Markdown, as image, text only, media only, or for Teams")
                        onClicked: copyMenu.popup(this, 0, height)
                        CopyMenu {
                            id: copyMenu
                            canvas: root.canvas
                        }
                    }
                    Tip {
                        visible: root.isNote && !root.formatting
                        iconName: "move-vertical"
                        tip: qsTr("Fit the height to the content")
                        onClicked: root.fitToContent()
                    }
                    SizePipette {
                        visible: !root.isStroke && !root.formatting
                        canvas: root.canvas
                        ids: [root.itemId]
                    }
                    Tip {
                        visible: root.childCount > 0 && root.connectable && !root.formatting
                        iconName: "network"
                        tip: qsTr("Tidy this branch")
                        onClicked: Board.tidy(root.itemId)
                    }
                    Tip {
                        visible: !root.formatting
                        iconName: "trash-2"
                        tip: qsTr("Delete (Del)")
                        onClicked: root.canvas.deleteSelected()
                    }
                }
            }
        }
    }

    // --- Resizing ------------------------------------------------------------
    // From any corner or side, as in drawing programs: Shift keeps the
    // proportions, Alt resizes around the middle. The moving edges snap to
    // other items (`rects`) and the grid, unless Space is held.
    function resizeFrom(handle, start, delta, modifiers, rects) {
        const keep = modifiers & Qt.ShiftModifier
        const fromMiddle = modifiers & Qt.AltModifier
        const factor = fromMiddle ? 2 : 1
        let w = start.width + handle.h * delta.x * factor
        let h = start.height + handle.v * delta.y * factor
        if (keep) {
            // Corners follow the side that moved most; a side brings the
            // other dimension along
            const s = handle.h !== 0 && handle.v !== 0 ? Math.max(w / start.width, h / start.height)
                    : handle.h !== 0 ? w / start.width : h / start.height
            const least = Math.max(minW / start.width, minH / start.height)
            w = start.width * Math.max(s, least)
            h = start.height * Math.max(s, least)
        } else {
            w = Math.max(minW, w)
            h = Math.max(minH, h)
        }
        // What stays: the middle, the opposite side, or (a side resized
        // with Shift) the middle across it
        const across = (side, size, startPos, startSize) =>
            fromMiddle || (side === 0 && keep) ? startPos + (startSize - size) / 2
            : side < 0 ? startPos + startSize - size : startPos
        let box = Qt.rect(across(handle.h, w, start.x, start.width), across(handle.v, h, start.y, start.height), w, h)
        // Proportions kept: snapping one edge would undo them
        box = keep ? box : canvas.snapResize(box, handle, fromMiddle, rects, minW, minH)
        // Whole pixels, as moving keeps them
        root.x = Math.round(box.x)
        root.y = Math.round(box.y)
        root.width = Math.round(box.width)
        root.height = Math.round(box.height)
    }

    Repeater {
        model: root.chrome && root.soleSelection && !root.isStroke
               ? [{h: -1, v: -1}, {h: 0, v: -1}, {h: 1, v: -1}, {h: 1, v: 0},
                  {h: 1, v: 1}, {h: 0, v: 1}, {h: -1, v: 1}, {h: -1, v: 0}]
               : []
        delegate: Item {
            id: handle
            required property var modelData
            readonly property bool corner: modelData.h !== 0 && modelData.v !== 0
            readonly property real knob: 10 * root.inv
            readonly property real grab: 12 * root.inv
            z: 10
            // Corners: a square; sides: the whole side between the corners
            width: modelData.h === 0 ? Math.max(0, root.width - 2 * knob) : grab
            height: modelData.v === 0 ? Math.max(0, root.height - 2 * knob) : grab
            x: modelData.h === 0 ? knob : modelData.h < 0 ? -grab / 2 : root.width - grab / 2
            y: modelData.v === 0 ? knob : modelData.v < 0 ? -grab / 2 : root.height - grab / 2

            Rectangle {
                visible: handle.corner
                anchors.centerIn: parent
                width: handle.knob
                height: handle.knob
                radius: 2 * root.inv
                color: Theme.accent
                border.color: Theme.s.background_elevated
                border.width: 1.5 * root.inv
            }

            MouseArea {
                anchors.fill: parent
                anchors.margins: handle.corner ? -4 * root.inv : 0
                cursorShape: !handle.corner ? (handle.modelData.h === 0 ? Qt.SizeVerCursor : Qt.SizeHorCursor)
                             : handle.modelData.h === handle.modelData.v ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                preventStealing: true

                property point start
                property rect startRect
                property var rects: []

                onPressed: (mouse) => {
                    start = mapToItem(root.parent, mouse.x, mouse.y)
                    startRect = Qt.rect(root.x, root.y, root.width, root.height)
                    rects = root.canvas.snapRects({ [root.itemId]: true })
                }
                onPositionChanged: (mouse) => {
                    const p = mapToItem(root.parent, mouse.x, mouse.y)
                    root.resizeFrom(handle.modelData, startRect, Qt.point(p.x - start.x, p.y - start.y),
                                    mouse.modifiers, rects)
                }
                onReleased: {
                    root.canvas.guides = []
                    root.commitGeometry()
                }
                onCanceled: {
                    root.canvas.guides = []
                    root.commitGeometry()
                }
            }
        }
    }
}
