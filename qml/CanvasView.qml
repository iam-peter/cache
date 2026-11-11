import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import QtMultimedia
import IgittStyle as Styles
import NoteApp

// The infinite canvas. "world" holds every note in canvas coordinates and is
// translated by (panX, panY) and scaled by zoom to map onto the viewport.
Item {
    id: view

    property real zoom: 1.0
    property real panX: 0
    property real panY: 0
    readonly property real minZoom: 0.05
    readonly property real maxZoom: 6.0

    property string selectedId: ""
    property string editingId: ""
    readonly property bool isEditing: editingId !== ""

    property point lastPointerWorld: Qt.point(0, 0)
    property bool pointerInside: false
    property var pendingView: null
    property bool pendingFit: false

    // Asks the note being edited to put media in at its cursor
    signal insertMediaRequested(string id, string path)

    clip: true
    focus: true
    activeFocusOnTab: true

    // --- Coordinates and view ------------------------------------------------

    function clampZoom(z) { return Math.max(minZoom, Math.min(maxZoom, z)) }
    function toWorld(px, py) { return Qt.point((px - panX) / zoom, (py - panY) / zoom) }
    function centerWorld() { return toWorld(width / 2, height / 2) }
    function insertionPoint() { return pointerInside ? lastPointerWorld : centerWorld() }

    function zoomAt(px, py, z) {
        viewAnim.stop()
        z = clampZoom(z)
        const w = toWorld(px, py)
        zoom = z
        panX = px - w.x * z
        panY = py - w.y * z
        saveViewSoon()
    }
    function zoomBy(factor) { zoomAt(width / 2, height / 2, zoom * factor) }

    function animateTo(z, px, py) {
        viewAnim.stop()
        zAnim.to = z
        xAnim.to = px
        yAnim.to = py
        viewAnim.start()
    }
    function centerOn(x, y, w, h, limit) {
        if (width <= 0 || height <= 0)
            return
        const margin = 60
        let z = Math.min((width - 2 * margin) / Math.max(w, 1), (height - 2 * margin) / Math.max(h, 1))
        z = clampZoom(Math.min(z, limit === undefined ? 1.0 : limit))
        animateTo(z, width / 2 - (x + w / 2) * z, height / 2 - (y + h / 2) * z)
    }
    function resetZoom() {
        const c = centerWorld()
        animateTo(1.0, width / 2 - c.x, height / 2 - c.y)
    }
    function fitAll() {
        if (width <= 0 || height <= 0) {
            pendingFit = true
            return
        }
        pendingFit = false
        const b = Board.itemsBounds()
        if (b.width <= 0 || b.height <= 0)
            animateTo(1.0, width / 2, height / 2)
        else
            centerOn(b.x, b.y, b.width, b.height, 1.0)
    }
    function applyView(cx, cy, z) {
        if (width <= 0 || height <= 0) {
            pendingView = { cx: cx, cy: cy, z: z }
            return
        }
        pendingView = null
        viewAnim.stop()
        zoom = clampZoom(z)
        panX = width / 2 - cx * zoom
        panY = height / 2 - cy * zoom
    }
    function applyPending() {
        if (width <= 0 || height <= 0)
            return
        if (pendingView)
            applyView(pendingView.cx, pendingView.cy, pendingView.z)
        if (pendingFit)
            fitAll()
    }
    function saveViewSoon() { viewSaveTimer.restart() }

    onWidthChanged: applyPending()
    onHeightChanged: applyPending()

    // --- Selection and editing ----------------------------------------------

    function select(id) {
        if (editingId !== "" && editingId !== id)
            editingId = ""
        selectedId = id
    }
    function selectRow(row) { if (row >= 0) select(Board.get(row).id) }
    function selectedRow() { return selectedId === "" ? -1 : Board.indexOf(selectedId) }
    function clearSelection() { editingId = ""; selectedId = "" }
    function startEditing(id) { selectedId = id; editingId = id }
    function finishEditing() { editingId = ""; view.forceActiveFocus() }

    function focusItem(id) {
        const row = Board.indexOf(id)
        if (row < 0)
            return
        const it = Board.get(row)
        select(id)
        centerOn(it.x, it.y, it.w, it.h, Math.max(1.0, zoom))
    }

    // --- Creating and changing notes ----------------------------------------

    function newNoteAt(pt, text) {
        const row = Board.addNote(pt.x - 170, pt.y - 40, text === undefined ? "" : text)
        if (row < 0)
            return
        const id = Board.get(row).id
        if (text)
            select(id)
        else
            startEditing(id)
    }
    function newNote() { newNoteAt(insertionPoint()) }

    // Images and videos always end up inside a note: at the cursor of the
    // one being edited, at the end of `targetRow`, or in a new note at pt.
    function addMedia(urls, pt, targetRow) {
        if (editingId !== "") {
            let inserted = 0
            for (const url of urls) {
                if (Board.isMediaFile(url.toString())) {
                    insertMediaRequested(editingId, url.toString())
                    inserted++
                }
            }
            if (inserted === 0)
                toast.show(qsTr("Only images and videos can go into a note"))
            return
        }
        const row = Board.importUrls(urls, pt.x, pt.y, targetRow === undefined ? -1 : targetRow)
        if (row >= 0)
            selectRow(row)
        else
            toast.show(qsTr("Nothing to add"))
    }
    // Pasting with a note selected adds to that note
    function pasteAt(pt) {
        const row = Board.pasteFromClipboard(pt.x, pt.y, selectedRow())
        if (row >= 0)
            selectRow(row)
        else
            toast.show(qsTr("The clipboard is empty"))
    }
    function deleteSelected() {
        const row = selectedRow()
        if (row < 0)
            return
        clearSelection()
        Board.remove(row)
    }
    function duplicateSelected() {
        const row = selectedRow()
        if (row >= 0)
            selectRow(Board.duplicate(row))
    }
    function copyRow(row) {
        if (row < 0)
            return
        Board.copyToClipboard(row)
        toast.show(qsTr("Note copied with its images. Paste it into Teams, mail or a document."))
    }
    function copySelected() { copyRow(selectedRow()) }
    function nudge(dx, dy) {
        const row = selectedRow()
        if (row < 0)
            return
        const it = Board.get(row)
        Board.setGeometry(row, it.x + dx, it.y + dy, it.w, it.h)
    }
    function openCardMenu(row) {
        cardMenu.row = row
        cardMenu.popup()
    }
    function showMessage(text) { toast.show(text) }
    // Pick images or videos for note `row`, the one being edited, or a new note
    function pickMedia(row) {
        mediaDialog.targetRow = row === undefined ? -1 : row
        mediaDialog.open()
    }
    function playVideo(url) {
        videoPopup.source = url
        videoPopup.open()
    }

    Connections {
        target: Board
        function onViewRestored() {
            if (Board.hasView) {
                view.applyView(Board.viewX, Board.viewY, Board.viewZoom)
            } else {
                view.applyView(0, 0, 1)
                Qt.callLater(view.fitAll)
            }
        }
    }

    Timer {
        id: viewSaveTimer
        interval: 400
        onTriggered: {
            const c = view.centerWorld()
            Board.setView(c.x, c.y, view.zoom)
        }
    }

    ParallelAnimation {
        id: viewAnim
        NumberAnimation { id: zAnim; target: view; property: "zoom"; duration: 260; easing.type: Easing.OutCubic }
        NumberAnimation { id: xAnim; target: view; property: "panX"; duration: 260; easing.type: Easing.OutCubic }
        NumberAnimation { id: yAnim; target: view; property: "panY"; duration: 260; easing.type: Easing.OutCubic }
        onFinished: view.saveViewSoon()
    }

    // --- Keyboard ------------------------------------------------------------

    Keys.onPressed: (event) => {
        if (view.isEditing)
            return
        const ctrl = event.modifiers & Qt.ControlModifier
        const step = (event.modifiers & Qt.ShiftModifier) ? 1 : 10
        if (event.matches(StandardKey.Paste))
            view.pasteAt(view.insertionPoint())
        else if (event.matches(StandardKey.Copy))
            view.copySelected()
        else if (event.matches(StandardKey.Redo) || (ctrl && event.key === Qt.Key_Z && (event.modifiers & Qt.ShiftModifier)))
            Board.redo()
        else if (event.matches(StandardKey.Undo))
            Board.undo()
        else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)
            view.deleteSelected()
        else if (ctrl && event.key === Qt.Key_D)
            view.duplicateSelected()
        else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && view.selectedId !== "")
            view.startEditing(view.selectedId)
        else if (event.key === Qt.Key_Escape)
            view.clearSelection()
        else if (event.key === Qt.Key_Left && view.selectedId !== "")
            view.nudge(-step, 0)
        else if (event.key === Qt.Key_Right && view.selectedId !== "")
            view.nudge(step, 0)
        else if (event.key === Qt.Key_Up && view.selectedId !== "")
            view.nudge(0, -step)
        else if (event.key === Qt.Key_Down && view.selectedId !== "")
            view.nudge(0, step)
        else
            return
        event.accepted = true
    }

    // --- Rendering and pointer input ----------------------------------------

    Rectangle {
        anchors.fill: parent
        color: Theme.canvas
    }

    GridBackground {
        anchors.fill: parent
        offsetX: view.panX
        offsetY: view.panY
        zoom: view.zoom
        color: Theme.gridDot
    }

    MouseArea {
        id: background
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        hoverEnabled: true

        property point pressPos
        property real startPanX
        property real startPanY
        property bool panning: false

        cursorShape: panning ? Qt.ClosedHandCursor : Qt.ArrowCursor

        onEntered: view.pointerInside = true
        onExited: view.pointerInside = false

        onPressed: (mouse) => {
            view.forceActiveFocus()
            viewAnim.stop()
            if (mouse.button === Qt.RightButton)
                return
            if (mouse.button === Qt.LeftButton)
                view.clearSelection()
            panning = true
            pressPos = Qt.point(mouse.x, mouse.y)
            startPanX = view.panX
            startPanY = view.panY
        }
        onPositionChanged: (mouse) => {
            view.lastPointerWorld = view.toWorld(mouse.x, mouse.y)
            if (panning) {
                view.panX = startPanX + mouse.x - pressPos.x
                view.panY = startPanY + mouse.y - pressPos.y
            }
        }
        onReleased: {
            if (panning)
                view.saveViewSoon()
            panning = false
        }
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                canvasMenu.worldPos = view.toWorld(mouse.x, mouse.y)
                canvasMenu.popup()
            }
        }
        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton)
                view.newNoteAt(view.toWorld(mouse.x, mouse.y))
        }
        onWheel: (wheel) => {
            view.lastPointerWorld = view.toWorld(wheel.x, wheel.y)
            const ctrl = wheel.modifiers & Qt.ControlModifier
            const shift = wheel.modifiers & Qt.ShiftModifier
            const touchpad = wheel.pixelDelta.x !== 0 || wheel.pixelDelta.y !== 0
            const wantsZoom = ctrl || (Theme.prefs.wheelZooms && !touchpad && !shift)
            if (wantsZoom) {
                const delta = touchpad ? wheel.pixelDelta.y / 40 : wheel.angleDelta.y / 120
                view.zoomAt(wheel.x, wheel.y, view.zoom * Math.pow(1.18, delta))
            } else {
                viewAnim.stop()
                let dx = touchpad ? wheel.pixelDelta.x : wheel.angleDelta.x / 2
                let dy = touchpad ? wheel.pixelDelta.y : wheel.angleDelta.y / 2
                if (shift && dx === 0) {
                    dx = dy
                    dy = 0
                }
                view.panX += dx
                view.panY += dy
                view.saveViewSoon()
            }
        }

        Item {
            id: world
            x: view.panX
            y: view.panY
            scale: view.zoom
            transformOrigin: Item.TopLeft

            Repeater {
                model: Board
                delegate: CanvasCard {
                    canvas: view
                }
            }
        }
    }

    PinchHandler {
        target: null
        property real startZoom: 1
        onActiveChanged: if (active) startZoom = view.zoom
        onActiveScaleChanged: view.zoomAt(centroid.position.x, centroid.position.y, startZoom * activeScale)
    }

    DropArea {
        id: dropArea
        anchors.fill: parent

        // The note under the pointer, which a drop adds to
        readonly property int targetRow: containsDrag ? Board.itemAt(view.toWorld(drag.x, drag.y).x,
                                                                      view.toWorld(drag.x, drag.y).y) : -1

        onDropped: (drop) => {
            const pt = view.toWorld(drop.x, drop.y)
            const target = view.isEditing ? -1 : Board.itemAt(pt.x, pt.y)
            drop.acceptProposedAction()
            if (drop.hasUrls) {
                view.addMedia(drop.urls, pt, target)
                return
            }
            if (view.isEditing)
                return
            for (const format of ["image/png", "image/jpeg", "image/webp", "image/gif", "application/x-qt-image"]) {
                if (drop.formats.indexOf(format) < 0)
                    continue
                const row = Board.addImageData(drop.getDataAsArrayBuffer(format), pt.x, pt.y, target)
                if (row >= 0) {
                    view.selectRow(row)
                    return
                }
            }
            if (drop.hasText && drop.text.trim().length > 0)
                view.newNoteAt(pt, drop.text)
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 6
        visible: dropArea.containsDrag
        radius: Theme.radius
        color: Styles.Primitives.alpha(Theme.accent, 0.06)
        border.color: Theme.accent
        border.width: 2

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            text: view.isEditing ? qsTr("Drop to insert into the note")
                 : dropArea.targetRow >= 0 ? qsTr("Drop to add to this note")
                 : qsTr("Drop to create a note")
            font.pixelSize: Styles.Primitives.fontSize.title
            color: Theme.accent
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Styles.Primitives.spacing.sp100
        visible: Board.count === 0 && Board.projectPath !== ""

        Icon {
            Layout.alignment: Qt.AlignHCenter
            name: "sticky-note"
            size: 40
            color: Theme.textMuted
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("This canvas is empty")
            font.pixelSize: Styles.Primitives.fontSize.title
            color: Theme.textMuted
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Double-click anywhere to write a note, or paste or drop images and videos")
            color: Theme.textMuted
        }
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
    }

    // Plays a video full size
    Popup {
        id: videoPopup
        property url source
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 1100)
        height: Math.min(parent.height - 80, width * 9 / 16 + 48)
        modal: true
        padding: 0
        onOpened: popupPlayer.play()
        onClosed: { popupPlayer.stop(); source = "" }

        MediaPlayer {
            id: popupPlayer
            source: videoPopup.source
            videoOutput: popupOutput
            audioOutput: AudioOutput { }
        }
        Rectangle {
            anchors.fill: popupOutput
            color: "#000000"
        }
        VideoOutput {
            id: popupOutput
            anchors.fill: parent
            anchors.bottomMargin: 48
        }
        RowLayout {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Styles.Primitives.spacing.sp100
            IconButton {
                iconName: popupPlayer.playbackState === MediaPlayer.PlayingState ? "pause" : "play"
                onClicked: popupPlayer.playbackState === MediaPlayer.PlayingState ? popupPlayer.pause() : popupPlayer.play()
            }
            Slider {
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, popupPlayer.duration)
                value: popupPlayer.position
                onMoved: popupPlayer.position = value
            }
            IconButton { iconName: "x"; onClicked: videoPopup.close() }
        }
    }

    FileDialog {
        id: mediaDialog
        property int targetRow: -1
        title: view.isEditing || targetRow >= 0 ? qsTr("Add images or videos to the note")
                                                : qsTr("Add images or videos in a new note")
        fileMode: FileDialog.OpenFiles
        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        nameFilters: [
            qsTr("Images and videos (*.png *.jpg *.jpeg *.gif *.webp *.bmp *.svg *.mp4 *.webm *.mkv *.mov *.m4v)"),
            qsTr("Markdown (*.md *.markdown *.txt)"),
        ]
        onAccepted: view.addMedia(selectedFiles, view.centerWorld(), targetRow)
    }

    Menu {
        id: canvasMenu
        property point worldPos
        MenuItem { text: qsTr("New note here"); onTriggered: view.newNoteAt(canvasMenu.worldPos) }
        MenuItem { text: qsTr("Paste here"); onTriggered: { view.clearSelection(); view.pasteAt(canvasMenu.worldPos) } }
        MenuSeparator {}
        MenuItem { text: qsTr("Fit everything"); onTriggered: view.fitAll() }
        MenuItem { text: qsTr("Zoom to 100%"); onTriggered: view.resetZoom() }
    }

    Menu {
        id: cardMenu
        property int row: -1
        MenuItem { text: qsTr("Edit"); onTriggered: view.startEditing(Board.get(cardMenu.row).id) }
        MenuItem { text: qsTr("Copy note with images"); onTriggered: view.copyRow(cardMenu.row) }
        MenuItem { text: qsTr("Zoom to note"); onTriggered: view.focusItem(Board.get(cardMenu.row).id) }
        MenuSeparator {}
        MenuItem { text: qsTr("Duplicate"); onTriggered: view.selectRow(Board.duplicate(cardMenu.row)) }
        MenuItem { text: qsTr("Bring to front"); onTriggered: Board.raise(cardMenu.row) }
        MenuItem { text: qsTr("Send to back"); onTriggered: Board.lower(cardMenu.row) }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Delete")
            onTriggered: {
                view.clearSelection()
                Board.remove(cardMenu.row)
            }
        }
    }
}
