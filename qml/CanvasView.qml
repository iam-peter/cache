import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Shapes
import QtQml.Models
import QtCore
import QtMultimedia
import IgittStyle as Styles
import Cache

// The infinite canvas. "world" holds every item in canvas coordinates and
// is translated by (panX, panY) and scaled by zoom to map onto the viewport.
Item {
    id: view

    property real zoom: 1.0
    property real panX: 0
    property real panY: 0
    readonly property real minZoom: 0.05
    readonly property real maxZoom: 6.0

    // select, hand, note, text, code, rect, ellipse, diamond, triangle,
    // section, connector, pen, highlighter
    property string tool: "select"
    property string penColor: ""

    property var selection: []
    readonly property string selectedId: selection.length > 0 ? selection[selection.length - 1] : ""
    property string editingId: ""
    property int editCursor: -1 // where the cursor goes when editing starts; -1: the end
    readonly property bool isEditing: editingId !== ""
    property string selectedLinkId: ""

    // Cards by item id, for connectors and moving several at once
    property var cards: ({})
    property int cardsVersion: 0

    property bool moving: false
    property var guides: []
    property bool spaceHeld: false
    // A connector being dragged out (or one end of one being moved): from
    // which item and side, and what is under the pointer
    property string linkFromId: ""
    property string linkFromSide: ""
    property point linkPoint
    property point linkStart
    property string linkTargetId: ""
    property string linkTargetSide: ""
    property bool linkLeftSource: false
    property string relinkId: ""   // the connector whose end is being moved
    // The size pipette while it is picking: { what: width|height|size, ids }
    property var sizePick: null
    // A note whose medium the editor's media list is on: outlined
    property string mediaHighlightId: ""
    property string sizePickHover: ""
    property string relinkEnd: ""  // "from" or "to"
    property bool exporting: false
    property var exportSet: ({})

    property point lastPointerWorld: Qt.point(0, 0)
    property bool pointerInside: false
    property var pendingView: null
    property bool pendingFit: false
    property string pendingFocusId: ""

    // Asks the note being edited to put Markdown in at its cursor
    signal insertMarkdownRequested(string id, string markdown)
    signal insertCodeRequested(string id)
    // A button of the editing toolbar: bold, h1, bullet, table, ...
    signal formatRequested(string id, string action)

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
    function panTo(cx, cy) {
        viewAnim.stop()
        panX = width / 2 - cx * zoom
        panY = height / 2 - cy * zoom
        saveViewSoon()
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
    // The selected items, as large as fits (up to 200%); everything when
    // nothing is selected
    function fitSelection() {
        if (selection.length === 0) {
            fitAll()
            return
        }
        const b = Board.boundsOf(selection)
        if (b.width > 0 && b.height > 0)
            centerOn(b.x, b.y, b.width, b.height, 2.0)
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

    // --- Cards ---------------------------------------------------------------

    function registerCard(id, card) {
        cards[id] = card
        cardsVersion++
    }
    function unregisterCard(id, card) {
        if (cards[id] === card) {
            delete cards[id]
            cardsVersion++
        }
    }

    // --- Selection and editing ----------------------------------------------

    function select(id) {
        if (editingId !== "" && editingId !== id)
            editingId = ""
        selectedLinkId = ""
        selection = id ? [id] : []
    }
    function setSelection(ids) {
        if (editingId !== "" && ids.indexOf(editingId) < 0)
            editingId = ""
        selectedLinkId = ""
        selection = ids
    }
    function toggleSelected(id) {
        const ids = selection.slice()
        const at = ids.indexOf(id)
        if (at >= 0)
            ids.splice(at, 1)
        else
            ids.push(id)
        setSelection(ids)
    }
    function selectRow(row) { if (row >= 0) select(Board.get(row).id) }
    function selectedRow() { return selectedId === "" ? -1 : Board.indexOf(selectedId) }
    // The size pipette: the next item clicked gives `ids` its width, height
    // or both
    function startSizePick(what, ids) {
        editingId = ""
        sizePick = { what: what, ids: ids.slice() }
        forceActiveFocus()
    }
    function finishSizePick(sourceId) {
        const pick = sizePick
        sizePick = null
        sizePickHover = ""
        if (!pick || sourceId === "")
            return
        if (Board.matchSize(pick.ids, sourceId, pick.what))
            setSelection(pick.ids.filter(id => Board.indexOf(id) >= 0))
    }
    function clearSelection() { editingId = ""; selectedLinkId = ""; selection = [] }
    function startEditing(id) {
        if (!id)
            return
        // Another board: opened, not edited
        if (cards[id] && cards[id].isBoard) {
            cards[id].openBoard()
            return
        }
        // A link's detail: to its card, if one shows it
        if (cards[id] && cards[id].isField) {
            const url = cards[id].itemText
            for (let row = 0; row < Board.count; ++row) {
                const it = Board.get(row)
                if (it.type === "link" && it.text === url && !it.hidden) {
                    focusItem(it.id)
                    return
                }
            }
            toast.show(qsTr("Its card is folded away or not on this board: Ctrl+click opens the link"))
            return
        }
        if (cards[id] && cards[id].itemLocked) {
            toast.show(qsTr("It's locked. Unlock it to change it."))
            return
        }
        selectedLinkId = ""
        selection = [id]
        editingId = id
    }
    function finishEditing() { editingId = ""; view.forceActiveFocus() }
    function selectLink(id) {
        editingId = ""
        selection = []
        selectedLinkId = id
    }

    function focusItem(id) {
        const row = Board.indexOf(id)
        if (row < 0)
            return
        const it = Board.get(row)
        if (it.hidden) { // in a folded branch: unfold its way there
            let up = Board.parentOf(id)
            while (up) {
                if (Board.getById(up).collapsed)
                    Board.toggleCollapsed(up)
                up = Board.parentOf(up)
            }
        }
        select(id)
        centerOn(it.x, it.y, it.w, it.h, Math.max(1.0, zoom))
    }
    // Opens a search result or link: switches project if needed
    function openNote(projectPath, id) {
        if (projectPath === Board.projectPath) {
            focusItem(id)
            return
        }
        const row = Workspace.indexOfPath(projectPath)
        if (row < 0)
            return
        clearSelection()
        pendingFocusId = id
        Workspace.currentIndex = row
    }
    // [[Target]]: the note, from any project; one that doesn't exist yet
    // is made next to the note that links to it
    function openWikiLink(target, fromId) {
        hideLinkPreview()
        const found = Search.findNote(target)
        if (found.found) {
            openNote(found.projectPath, found.noteId)
            return
        }
        const from = Board.getById(fromId)
        // [[Note#Heading]] to a missing note makes the note
        const name = target.indexOf("#") > 0 ? target.substring(0, target.indexOf("#")) : target
        const title = name.indexOf("/") > 0 ? name.substring(name.indexOf("/") + 1) : name
        const row = Board.addNote(from.x !== undefined ? from.x + from.w + 80 : centerWorld().x,
                                  from.y !== undefined ? from.y : centerWorld().y, "# " + title + "\n\n")
        if (row >= 0)
            startEditing(Board.get(row).id)
    }

    // --- Creating and changing items ----------------------------------------

    // `area`: the rectangle it was dragged out to; none for the usual size,
    // around `pt`
    function newNoteAt(pt, text, area) {
        const row = area ? Board.addNote(area.x, area.y, text === undefined ? "" : text, area.width, area.height)
                         : Board.addNote(pt.x - 170, pt.y - 40, text === undefined ? "" : text)
        if (row < 0)
            return
        const id = Board.get(row).id
        if (text)
            select(id)
        else
            startEditing(id)
    }
    function newNote() { newNoteAt(insertionPoint()) }
    function newCodeNoteAt(pt, area) {
        const row = area ? Board.addNote(area.x, area.y, "```\n\n```\n", area.width, area.height)
                         : Board.addNote(pt.x - 200, pt.y - 60, "```\n\n```\n")
        if (row < 0)
            return
        editCursor = 3 // after the opening ```: type the language, then Enter
        startEditing(Board.get(row).id)
    }
    // A code block in this note, at the cursor if it is being edited
    function insertCode(id) {
        if (editingId !== id) {
            const row = Board.indexOf(id)
            const text = Board.get(row).text
            editCursor = text.length
            startEditing(id)
        }
        insertCodeRequested(id)
    }
    function newItem(type, kind, x, y, w, h) {
        const row = Board.addItem(type, x, y, w, h, kind, "")
        if (row < 0)
            return ""
        const id = Board.get(row).id
        if (type === "shape")
            select(id)
        else
            startEditing(id)
        return id
    }

    // Files always end up inside a note: at the cursor of the one being
    // edited, at the end of `targetRow`, or in a new note at pt. Images and
    // videos show, other files are attached.
    function addMedia(urls, pt, targetRow) {
        if (editingId !== "") {
            const md = Board.filesMarkdown(urls)
            if (md)
                insertMarkdownRequested(editingId, md)
            else
                toast.show(qsTr("Nothing to add"))
            return
        }
        const row = Board.importUrls(urls, pt.x, pt.y, targetRow === undefined ? -1 : targetRow)
        if (row >= 0)
            selectRow(row)
        else
            toast.show(qsTr("Nothing to add"))
    }
    // Pasting with a note selected adds media to that note
    function pasteAt(pt) {
        const row = selectedRow()
        const target = row >= 0 && Board.get(row).type === "note" ? row : -1
        const ids = Board.pasteFromClipboard(pt.x, pt.y, target)
        if (ids.length > 0)
            setSelection(ids)
        else
            toast.show(qsTr("The clipboard is empty"))
    }
    function deleteSelected() {
        if (selectedLinkId !== "") {
            Board.removeLink(selectedLinkId)
            selectedLinkId = ""
            return
        }
        const ids = selection
        if (ids.length === 0)
            return
        const locked = ids.filter(id => cards[id] && cards[id].itemLocked)
        clearSelection()
        Board.removeItems(ids)
        if (locked.length > 0) {
            // What stays stays selected
            setSelection(locked)
            toast.show(locked.length === ids.length ? qsTr("Locked items aren't deleted. Unlock them first.")
                                                    : locked.length === 1
                                                      ? qsTr("1 locked item kept: unlock it to delete it.")
                                                      : qsTr("%1 locked items kept: unlock them to delete them.").arg(locked.length))
        }
    }
    // A link card for `url` in the middle of the view, selected
    function addLinkCard(url) {
        const c = centerWorld()
        const row = Board.addItem("link", Math.round(c.x - 160), Math.round(c.y - 100), 320, 200, "", url)
        if (row >= 0)
            setSelection([Board.get(row).id])
        return row
    }
    // A card of another project of the workspace, by its folder's name
    function addBoardCard(project) {
        const c = centerWorld()
        const row = Board.addItem("board", Math.round(c.x - 180), Math.round(c.y - 130), 360, 260, "", project)
        if (row >= 0)
            setSelection([Board.get(row).id])
    }
    // Cards of the changes issue card `id` led to (Gerrit's), beside it and
    // connected to it; those already on the board are left as they are
    property var relatedFor: null // {id, url} while Gerrit is asked
    function addRelatedCards(id) {
        const item = Board.getById(id)
        if (!item || item.type !== "link" || !Links.canRelate(item.text))
            return
        relatedFor = { id: id, url: item.text }
        toast.show(qsTr("Asking Gerrit for the changes of %1…").arg(Links.issueKey(item.text)))
        Links.related(item.text)
    }
    Connections {
        target: Links
        function onRelatedFound(url, key, results) {
            const wanted = view.relatedFor
            if (!wanted || wanted.url !== url)
                return
            view.relatedFor = null
            const issue = Board.getById(wanted.id)
            if (!issue || !issue.id)
                return
            if (results.length === 0) {
                toast.show(qsTr("Gerrit has no changes naming %1").arg(key))
                return
            }
            const there = new Set()
            for (let row = 0; row < Board.count; ++row) {
                const it = Board.get(row)
                if (it.type === "link")
                    there.add(it.text)
            }
            const fresh = results.filter(r => !there.has(r.url))
            if (fresh.length === 0) {
                toast.show(qsTr("The changes of %1 are on the board already").arg(key))
                return
            }
            // A column to the right of the issue
            const w = 320, h = 190, gap = 24
            const x = Math.round(issue.x + issue.w + 96)
            let y = Math.round(issue.y)
            const added = []
            for (const r of fresh) {
                const row = Board.addItem("link", x, y, w, h, "", r.url)
                if (row < 0)
                    continue
                const id = Board.get(row).id
                Board.addLink(wanted.id, id, "right", "left")
                added.push(id)
                y += h + gap
            }
            setSelection(added)
            // The issue and its changes in view, not zoomed in for it
            const b = Board.boundsOf([wanted.id].concat(added))
            const from = toWorld(0, 0), to = toWorld(view.width, view.height)
            if (b.x < from.x || b.y < from.y || b.x + b.width > to.x || b.y + b.height > to.y)
                centerOn(b.x, b.y, b.width, b.height, view.zoom)
            toast.show(added.length === 1 ? qsTr("Added 1 change of %1").arg(key)
                                          : qsTr("Added %1 changes of %2").arg(added.length).arg(key))
        }
    }
    Binding {
        target: Theme
        property: "canvasZoom"
        value: view.zoom
    }
    // Detail `key` of link card `cardId` (":status", a field's name…) as a
    // node of its own on `side` (right, left, top or bottom): outside the
    // card's section if it is in one, so folding the section keeps it in
    // sight; past what is there already
    function pullOut(cardId, key, side) {
        const card = Board.getById(cardId)
        if (!card || card.type !== "link")
            return
        side = side || "right"
        const section = card.section ? Board.getById(card.section) : null
        const outer = section || card
        const w = 200, h = 44, gap = 24
        // A section's title sits above it
        const above = section ? 40 : 0
        let x = side === "right" ? outer.x + outer.w + gap : side === "left" ? outer.x - gap - w : card.x
        let y = side === "bottom" ? outer.y + outer.h + gap : side === "top" ? outer.y - above - gap - h : card.y
        // Along the side: down beside it, rightwards above or below it
        const across = side === "left" || side === "right"
        const taken = (r) => {
            for (let row = 0; row < Board.count; ++row) {
                const it = Board.get(row)
                if (it.hidden || it.type === "section" || it.type === "stroke")
                    continue
                if (it.x < r.x + r.w && it.x + it.w > r.x && it.y < r.y + r.h && it.y + it.h > r.y)
                    return it
            }
            return null
        }
        for (let i = 0; i < 200; ++i) {
            const it = taken({ x: x, y: y, w: w, h: h })
            if (!it)
                break
            if (across)
                y = it.y + it.h + 8
            else
                x = it.x + it.w + 8
        }
        const opposite = { right: "left", left: "right", top: "bottom", bottom: "top" }
        const row = Board.addField(cardId, Math.round(x), Math.round(y), w, h, key, side, opposite[side])
        if (row >= 0)
            setSelection([Board.get(row).id])
    }
    // An empty one, its address being asked for
    function newLinkCard() {
        const row = addLinkCard("")
        if (row >= 0)
            startEditing(Board.get(row).id)
    }
    // To the item a connector comes from, or (toEnd) points to
    function followLink(linkId, toEnd) {
        const l = Board.links.find(x => x.id === linkId)
        if (!l)
            return
        const target = toEnd ? l.to : l.from
        focusItem(target)
        setSelection([target])
    }

    // The selected notes, each as tall as what it holds
    function fitToContent() {
        for (const id of selection) {
            if (cards[id])
                cards[id].fitToContent()
        }
    }
    function duplicateSelected() {
        const ids = Board.duplicateItems(selection)
        if (ids.length > 0)
            setSelection(ids)
    }
    function copyRow(row) {
        if (row < 0)
            return
        Board.copyToClipboard(row)
        toast.show(qsTr("Note copied with its images. Paste it into Teams, mail or a document."))
    }
    // One note: as rich text for Teams; several items: to paste on a
    // canvas, with their text
    function copySelected() {
        if (selection.length === 1 && Board.getById(selectedId).type === "note")
            copyRow(selectedRow())
        else if (selection.length > 0) {
            Board.copyItems(selection)
            toast.show(qsTr("Copied %1 items").arg(selection.length))
        }
    }
    // The selected notes as "markdown" (as it is), "text" (without media,
    // mentions named after the files "media" copies) or "media" (their
    // images and files): for a chat's prompt box, which takes text or files
    // in one paste, never both
    function copyNotes(mode) {
        if (selection.length === 0)
            return
        const result = Board.copyNotes(selection, mode)
        if (mode === "media") {
            const n = result.files ? result.files.length : 0
            toast.show(n === 0 ? qsTr("No images or files to copy")
                       : n === 1 ? qsTr("Copied 1 file, named as the note names it. Paste it into the chat.")
                       : qsTr("Copied %1 files, named as the notes name them. Paste them into the chat.").arg(n), 5)
        } else if (!result.text) {
            toast.show(qsTr("Nothing to copy: no text in the selection"))
        } else if (mode === "text" && result.attachments > 0) {
            toast.show(qsTr("Copied the text. Its images and files are named as Copy › Media only names them."), 5)
        } else {
            toast.show(mode === "text" ? qsTr("Copied the text") : qsTr("Copied the Markdown"))
        }
    }
    function panBy(dx, dy) {
        viewAnim.stop()
        panX += dx
        panY += dy
        saveViewSoon()
    }
    function nudge(dx, dy) {
        if (selection.length === 0)
            return
        const positions = []
        for (const id of selection) {
            const it = Board.getById(id)
            positions.push({ id: id, x: it.x + dx, y: it.y + dy })
        }
        Board.moveItems(positions)
    }
    function selectAll() {
        const ids = []
        for (let i = 0; i < Board.count; ++i) {
            const it = Board.get(i)
            if (!it.hidden)
                ids.push(it.id)
        }
        setSelection(ids)
    }
    // The details of link card `cardId` to pull out on `side`, as a menu
    function openPullMenu(cardId, side) {
        const card = cards[cardId]
        const parts = card ? card.pullable : []
        if (parts.length === 0) {
            toast.show(qsTr("This card has no details to pull out yet"))
            return
        }
        pullMenu.cardId = cardId
        pullMenu.side = side || "right"
        pullMenu.parts = parts
        pullMenu.popup()
    }
    Menu {
        id: pullMenu
        property string cardId
        property string side: "right"
        property var parts: []
        MenuItem {
            text: qsTr("Pull out as a node")
            enabled: false
        }
        Instantiator {
            model: pullMenu.parts
            delegate: MenuItem {
                required property var modelData
                readonly property string shortValue: modelData.value.length > 36 ? modelData.value.slice(0, 35) + "…"
                                                                                : modelData.value
                text: shortValue !== "" ? qsTr("%1: %2").arg(modelData.name, shortValue) : modelData.name
                onTriggered: view.pullOut(pullMenu.cardId, modelData.key, pullMenu.side)
            }
            onObjectAdded: (index, object) => pullMenu.insertItem(index + 1, object)
            onObjectRemoved: (index, object) => pullMenu.removeItem(object)
        }
    }
    function openCardMenu(row) {
        cardMenu.row = row
        cardMenu.popup()
    }
    function showMessage(text) { toast.show(text) }
    function saveTemplate() { templateDialog.saveSelection() }
    function playVideo(url) {
        videoPopup.source = url
        videoPopup.open()
    }
    // Pick images or videos for note `row`, the one being edited, or a new note
    function pickMedia(row) {
        mediaDialog.targetRow = row === undefined ? -1 : row
        mediaDialog.open()
    }

    // --- Mindmap keys ----------------------------------------------------------

    function addChild() {
        if (!selectedId)
            return
        const id = Board.addChild(selectedId)
        if (id)
            startEditing(id)
    }
    function addSibling() {
        const id = Board.addSibling(selectedId)
        if (id)
            startEditing(id)
    }
    function goTo(id) {
        if (!id)
            return
        select(id)
        const it = Board.getById(id)
        const c = cards[id]
        // Into view if it isn't
        const left = it.x * zoom + panX, top = it.y * zoom + panY
        if (left < 0 || top < 0 || left + it.w * zoom > width || top + it.h * zoom > height)
            centerOn(it.x, it.y, it.w, it.h, zoom)
    }
    function navigate(key) {
        const id = selectedId
        if (!id)
            return
        if (key === Qt.Key_Left) {
            goTo(Board.parentOf(id))
        } else if (key === Qt.Key_Right) {
            if (Board.getById(id).collapsed)
                Board.toggleCollapsed(id)
            goTo(Board.childrenOf(id)[0])
        } else {
            const parent = Board.parentOf(id)
            if (!parent)
                return
            const siblings = Board.childrenOf(parent)
            const at = siblings.indexOf(id) + (key === Qt.Key_Up ? -1 : 1)
            if (at >= 0 && at < siblings.length)
                goTo(siblings[at])
        }
    }

    // --- Moving, with smart guides -------------------------------------------

    property var moveState: null

    // What moving and resizing snap to: the other items' edges and middles
    // (the smart guides) and, if set, the dot grid. Space held turns it off.
    readonly property real gridSize: 24
    function snapRects(excluded) {
        const rects = []
        for (const key in cards) {
            const c = cards[key]
            if (!excluded[key] && c.visible && !c.isStroke)
                rects.push(Qt.rect(c.x, c.y, c.width, c.height))
        }
        return rects
    }
    // Of `edges`, the one nearest to a guide: {index, delta, at}, at null
    // when none is within reach
    function snapEdges(edges, vertical, rects) {
        let best = { index: -1, delta: 0, at: null, d: 6 / zoom }
        if (Theme.prefs.snap) {
            for (const r of rects) {
                const targets = vertical ? [r.x, r.x + r.width / 2, r.x + r.width]
                                         : [r.y, r.y + r.height / 2, r.y + r.height]
                for (let i = 0; i < edges.length; ++i) {
                    for (const t of targets) {
                        const d = Math.abs(t - edges[i])
                        if (d < best.d)
                            best = { index: i, delta: t - edges[i], at: t, d: d }
                    }
                }
            }
        }
        if (best.at === null && Theme.prefs.snapGrid) {
            best.d = Infinity
            for (let i = 0; i < edges.length; ++i) {
                const t = Math.round(edges[i] / gridSize) * gridSize
                if (Math.abs(t - edges[i]) < best.d)
                    best = { index: i, delta: t - edges[i], at: null, d: Math.abs(t - edges[i]) }
            }
        }
        return best
    }
    // Spacing on one axis ("x" or "y") for box `b` where it is being moved
    // to: the same gap on both sides between its two nearest neighbours in
    // its row (or column), a gap other items keep from each other, or the
    // padding other items keep inside the section it lies in (24 at
    // least). {delta, lines} with measurement lines, or null when none is
    // within reach.
    function snapGaps(b, axis, items, sections) {
        const horizontal = axis === "x"
        const lo = r => horizontal ? r.x : r.y
        const hi = r => horizontal ? r.x + r.width : r.y + r.height
        const crossLo = r => horizontal ? r.y : r.x
        const crossHi = r => horizontal ? r.y + r.height : r.x + r.width
        const across = (r, t) => crossLo(r) < crossHi(t) && crossLo(t) < crossHi(r)
        const tolerance = 6 / zoom
        const size = hi(b) - lo(b)

        // Its nearest neighbours, before and after it
        let before = null, after = null
        for (const r of items) {
            if (!across(r, b))
                continue
            if (hi(r) <= lo(b) + tolerance && (before === null || hi(r) > hi(before)))
                before = r
            if (lo(r) >= hi(b) - tolerance && (after === null || lo(r) < lo(after)))
                after = r
        }
        // The distances others keep, in either direction (a column's gaps
        // serve a row too): each item to its nearest neighbour after it, and
        // below it; then the padding inside the section, further down
        const gaps = []
        const keep = (d) => {
            d = Math.round(d)
            if (d > 0 && d < 400 && gaps.indexOf(d) < 0)
                gaps.push(d)
        }
        for (const along of [true, false]) {
            const l = r => along === horizontal ? lo(r) : crossLo(r)
            const h = r => along === horizontal ? hi(r) : crossHi(r)
            const facing = (r, t) => along === horizontal ? across(r, t)
                                                          : lo(r) < hi(t) && lo(t) < hi(r)
            for (let i = 0; i < items.length; ++i) {
                let next = -1
                for (let j = 0; j < items.length; ++j) {
                    if (j !== i && facing(items[i], items[j]) && l(items[j]) >= h(items[i])
                            && (next < 0 || l(items[j]) < l(items[next])))
                        next = j
                }
                if (next >= 0)
                    keep(l(items[next]) - h(items[i]))
            }
        }

        // The section it lies in (the smallest), and the padding the others
        // in it keep from any of its sides; a padding serves as a gap
        // between them, and their gaps as a padding
        const cx = b.x + b.width / 2, cy = b.y + b.height / 2
        let parent = null
        for (const sec of sections) {
            if (cx > sec.x && cx < sec.x + sec.width && cy > sec.y && cy < sec.y + sec.height
                    && (parent === null || sec.width * sec.height < parent.width * parent.height))
                parent = sec
        }
        const pads = []
        if (parent !== null) {
            pads.push(24)
            for (const r of items) {
                const inside = r.x >= parent.x && r.y >= parent.y && r.x + r.width <= parent.x + parent.width
                               && r.y + r.height <= parent.y + parent.height
                if (!inside)
                    continue
                for (const pad of [r.x - parent.x, parent.x + parent.width - r.x - r.width,
                                   r.y - parent.y, parent.y + parent.height - r.y - r.height]) {
                    const p = Math.round(pad)
                    if (p > 0 && p < 200 && pads.indexOf(p) < 0)
                        pads.push(p)
                }
            }
            for (const gap of gaps.slice()) {
                if (gap < 200 && pads.indexOf(gap) < 0)
                    pads.push(gap)
            }
            for (const pad of pads)
                keep(pad)
        }

        // Where it may go, and the gaps that measure it there
        const candidates = []
        if (before !== null && after !== null) {
            const to = (hi(before) + lo(after) - size) / 2
            if (to > hi(before))
                candidates.push({ to: to, gaps: [{ from: hi(before), size: to - hi(before), other: before },
                                                  { from: to + size, size: to - hi(before), other: after }] })
        }
        for (const gap of gaps) {
            if (before !== null)
                candidates.push({ to: hi(before) + gap, gaps: [{ from: hi(before), size: gap, other: before }] })
            if (after !== null)
                candidates.push({ to: lo(after) - gap - size, gaps: [{ from: lo(after) - gap, size: gap, other: after }] })
        }
        for (const pad of pads) {
            candidates.push({ to: lo(parent) + pad, gaps: [{ from: lo(parent), size: pad, other: null }] })
            candidates.push({ to: hi(parent) - pad - size, gaps: [{ from: hi(parent) - pad, size: pad, other: null }] })
        }

        let best = null
        for (const c of candidates) {
            const d = Math.abs(c.to - lo(b))
            if (d < tolerance && (best === null || d < best.d))
                best = { d: d, c: c }
        }
        if (best === null)
            return null
        const delta = best.c.to - lo(b)
        const moved = horizontal ? Qt.rect(b.x + delta, b.y, b.width, b.height) : Qt.rect(b.x, b.y + delta, b.width, b.height)
        // Across each gap, in the middle of where the two face each other
        const lines = best.c.gaps.map(g => {
            const at = g.other === null ? (crossLo(moved) + crossHi(moved)) / 2
                     : (Math.max(crossLo(moved), crossLo(g.other)) + Math.min(crossHi(moved), crossHi(g.other))) / 2
            const label = String(Math.round(g.size))
            return horizontal ? { x1: g.from, y1: at, x2: g.from + g.size, y2: at, label: label }
                              : { x1: at, y1: g.from, x2: at, y2: g.from + g.size, label: label }
        })
        return { delta: delta, lines: lines }
    }
    // A guide along `at`, over the box and what it lines up with
    function guideAlong(vertical, at, box, rects) {
        let lo = vertical ? box.y : box.x
        let hi = vertical ? box.y + box.height : box.x + box.width
        for (const r of rects) {
            const edges = vertical ? [r.x, r.x + r.width / 2, r.x + r.width] : [r.y, r.y + r.height / 2, r.y + r.height]
            if (edges.some(e => Math.abs(e - at) < 0.5)) {
                lo = Math.min(lo, vertical ? r.y : r.x)
                hi = Math.max(hi, vertical ? r.y + r.height : r.x + r.width)
            }
        }
        return vertical ? { x1: at, y1: lo, x2: at, y2: hi } : { x1: lo, y1: at, x2: hi, y2: at }
    }
    // A resized item's new box, its moving edges snapped. `handle` says
    // which sides move ({h, v}: -1 start, 1 end, 0 neither); around the
    // middle, both do.
    function snapResize(r, handle, fromMiddle, rects, minW, minH) {
        if (spaceHeld || (!Theme.prefs.snap && !Theme.prefs.snapGrid)) {
            guides = []
            return r
        }
        let box = Qt.rect(r.x, r.y, r.width, r.height)
        const lines = []
        for (const vertical of [true, false]) {
            const side = vertical ? handle.h : handle.v
            if (side === 0)
                continue
            const pos = vertical ? box.x : box.y
            const size = vertical ? box.width : box.height
            const edges = fromMiddle ? [pos, pos + size] : [side < 0 ? pos : pos + size]
            const best = snapEdges(edges, vertical, rects)
            if (best.index < 0)
                continue
            const atStart = fromMiddle ? best.index === 0 : side < 0
            const grow = (atStart ? -best.delta : best.delta) * (fromMiddle ? 2 : 1)
            if (size + grow < (vertical ? minW : minH))
                continue
            const newPos = atStart ? pos + best.delta : fromMiddle ? pos - best.delta : pos
            box = vertical ? Qt.rect(newPos, box.y, size + grow, box.height)
                           : Qt.rect(box.x, newPos, box.width, size + grow)
            if (best.at !== null)
                lines.push(guideAlong(vertical, best.at, box, rects))
        }
        guides = lines
        return box
    }

    function beginMove(id, p) {
        const ids = selection.indexOf(id) >= 0 ? selection.slice() : [id]
        // A section takes along what lies in it
        for (const sid of ids.slice()) {
            const c = cards[sid]
            if (c && c.isSection) {
                for (const inner of Board.contentsOf(sid)) {
                    if (ids.indexOf(inner) < 0)
                        ids.push(inner)
                }
            }
        }
        // Locked ones stay where they are
        for (let i = ids.length - 1; i >= 0; --i) {
            if (cards[ids[i]] && cards[ids[i]].itemLocked)
                ids.splice(i, 1)
        }
        const starts = {}
        for (const mid of ids) {
            const c = cards[mid]
            if (c)
                starts[mid] = Qt.point(c.x, c.y)
        }
        const moved = {}
        for (const mid of ids)
            moved[mid] = true
        // For spacing: the items it may line up with, and the sections it may lie in
        const items = [], sections = []
        for (const key in cards) {
            const c = cards[key]
            if (moved[key] || !c.visible || c.isStroke)
                continue
            ;(c.isSection ? sections : items).push(Qt.rect(c.x, c.y, c.width, c.height))
        }
        moveState = { ids: ids, starts: starts, origin: p, others: snapRects(moved), copied: false,
                      items: items, sections: sections,
                      box: Board.boundsOf(selection.indexOf(id) >= 0 ? selection : [id]), moved: false }
    }
    // Alt as the move starts: copies go where it is dragged, the originals
    // stay (and keep their connectors)
    function copyForMove(st) {
        const copies = Board.duplicateItems(st.ids)
        const starts = {}
        const kept = []
        for (const id of copies) {
            const c = cards[id]
            if (!c)
                continue
            // Duplicates land a little off; they start where the originals are
            starts[id] = Qt.point(c.x - 28, c.y - 28)
            kept.push(id)
        }
        for (const id of st.ids) {
            const c = cards[id]
            if (c)
                st.others.push(Qt.rect(c.x, c.y, c.width, c.height))
        }
        st.ids = kept
        st.starts = starts
        st.copied = true
        setSelection(kept)
    }
    function updateMove(p, modifiers) {
        const st = moveState
        if (!st)
            return
        let dx = p.x - st.origin.x
        let dy = p.y - st.origin.y
        if (!st.moved && Math.hypot(dx, dy) * zoom < 3)
            return
        if (!st.moved && (modifiers & Qt.AltModifier))
            copyForMove(st)
        st.moved = true
        moving = true
        // Shift: along one direction only
        const axis = (modifiers & Qt.ShiftModifier) ? (Math.abs(dx) >= Math.abs(dy) ? "x" : "y") : ""
        if (axis === "x")
            dy = 0
        else if (axis === "y")
            dx = 0
        const lines = []
        if (!spaceHeld) {
            const b = st.box
            const sx = axis === "y" ? { index: -1 }
                     : snapEdges([b.x + dx, b.x + b.width / 2 + dx, b.x + b.width + dx], true, st.others)
            const sy = axis === "x" ? { index: -1 }
                     : snapEdges([b.y + dy, b.y + b.height / 2 + dy, b.y + b.height + dy], false, st.others)
            // Spacing, where no edge lines up as closely (the grid only
            // when neither does)
            const at = Qt.rect(b.x + dx, b.y + dy, b.width, b.height)
            const gx = axis === "y" || !Theme.prefs.snap ? null : snapGaps(at, "x", st.items, st.sections)
            const gy = axis === "x" || !Theme.prefs.snap ? null : snapGaps(at, "y", st.items, st.sections)
            const useGx = gx !== null && (sx.index < 0 || sx.at === null || Math.abs(gx.delta) < Math.abs(sx.delta) - 0.01)
            const useGy = gy !== null && (sy.index < 0 || sy.at === null || Math.abs(gy.delta) < Math.abs(sy.delta) - 0.01)
            if (useGx)
                dx += gx.delta
            else if (sx.index >= 0)
                dx += sx.delta
            if (useGy)
                dy += gy.delta
            else if (sy.index >= 0)
                dy += sy.delta
            const moved = Qt.rect(b.x + dx, b.y + dy, b.width, b.height)
            if (useGx)
                lines.push(...gx.lines)
            else if (sx.index >= 0 && sx.at !== null)
                lines.push(guideAlong(true, sx.at, moved, st.others))
            if (useGy)
                lines.push(...gy.lines)
            else if (sy.index >= 0 && sy.at !== null)
                lines.push(guideAlong(false, sy.at, moved, st.others))
        }
        guides = lines
        for (const id of st.ids) {
            const c = cards[id]
            if (c && st.starts[id]) {
                c.x = st.starts[id].x + dx
                c.y = st.starts[id].y + dy
            }
        }
    }
    function endMove() {
        const st = moveState
        moveState = null
        moving = false
        guides = []
        if (!st)
            return
        if (st.moved) {
            const positions = []
            for (const id of st.ids) {
                const c = cards[id]
                if (c)
                    positions.push({ id: id, x: c.x, y: c.y })
            }
            // Copying and moving the copies: one step to undo
            if (Board.moveItems(positions) && st.copied)
                Board.mergeUndo()
        }
        for (const id of st.ids) {
            if (cards[id])
                cards[id].rebind()
        }
    }

    // --- Connecting ------------------------------------------------------------

    function insideCard(id, p) {
        const c = cards[id]
        return c !== undefined && p.x >= c.x && p.x <= c.x + c.width && p.y >= c.y && p.y <= c.y + c.height
    }
    // The side of an item nearest to a point
    function nearestSide(id, p) {
        const c = cards[id]
        if (!c)
            return ""
        const d = [["top", Math.abs(p.y - c.y)], ["right", Math.abs(c.x + c.width - p.x)],
                   ["bottom", Math.abs(c.y + c.height - p.y)], ["left", Math.abs(p.x - c.x)]]
        d.sort((a, b) => a[1] - b[1])
        return d[0][0]
    }
    // From an item, out of a side (or any, if none)
    function beginLink(id, p, side) {
        linkFromId = id
        linkFromSide = side || ""
        linkPoint = p
        linkStart = p
        linkTargetId = ""
        linkTargetSide = ""
        linkLeftSource = !insideCard(id, p)
    }
    // One end of an existing connector onto another side or item
    function beginRelink(linkId, end, p) {
        const l = Board.links.find(x => x.id === linkId)
        if (!l)
            return
        relinkId = linkId
        relinkEnd = end
        beginLink(end === "to" ? l.from : l.to, p, end === "to" ? l.fromSide : l.toSide)
        linkLeftSource = true
    }
    function updateLink(p) {
        linkPoint = p
        if (!insideCard(linkFromId, p))
            linkLeftSource = true
        // What is under the pointer: the item it started from too, once the
        // pointer has been outside it, for a loop
        let target = Board.targetAt(p.x, p.y)
        if (target === linkFromId && !linkLeftSource)
            target = ""
        linkTargetId = target
        linkTargetSide = target ? nearestSide(target, p) : ""
    }
    // Onto an item: connects into the side nearest the pointer. Onto empty
    // canvas: a new connected note there, as in FigJam.
    function endLink(p) {
        if (!linkFromId)
            return
        updateLink(p)
        const from = linkFromId, fromSide = linkFromSide
        const target = linkTargetId, toSide = linkTargetSide
        const moved = Math.hypot(p.x - linkStart.x, p.y - linkStart.y) * zoom > 6
        const relink = relinkId, end = relinkEnd
        linkFromId = ""
        linkTargetId = ""
        linkTargetSide = ""
        relinkId = ""
        relinkEnd = ""
        if (!moved)
            return // a click
        if (relink) {
            if (target)
                Board.setLink(relink, end === "to" ? { to: target, toSide: toSide } : { from: target, fromSide: toSide })
            selectLink(relink)
            return
        }
        if (target) {
            selectLink(Board.addLink(from, target, fromSide, toSide))
        } else if (!insideCard(from, p)) {
            startEditing(Board.addConnectedNote(from, p.x - 110, p.y - 50, fromSide))
        }
    }
    function linkAt(p) {
        let best = ""
        let distance = 8 / zoom
        for (let i = 0; i < linkRepeater.count; ++i) {
            const c = linkRepeater.itemAt(i)
            if (!c || !c.shown)
                continue
            const d = c.distanceTo(p)
            if (d < distance) {
                distance = d
                best = c.link.id
            }
        }
        return best
    }
    function connectorFor(id) {
        for (let i = 0; i < linkRepeater.count; ++i) {
            const c = linkRepeater.itemAt(i)
            if (c && c.link.id === id)
                return c
        }
        return null
    }

    // --- Link previews and backlinks -------------------------------------------

    function showLinkPreview(target, scenePos) {
        linkPreview.target = target
        linkPreview.anchor = view.mapFromItem(null, scenePos.x, scenePos.y)
        previewTimer.restart()
    }
    function hideLinkPreview() {
        previewTimer.stop()
        linkPreview.close()
    }
    // A web link that Jira or the like knows: its details, a moment after
    // the pointer came onto it
    // Any web link in a note: its details, a moment after the pointer came
    // to rest on it. Only then is it read: passing over links fetches none.
    function showLinkCard(url, scenePos) {
        if ((linkCard.url === url || linkCard.pending === url) && (linkCard.opened || linkCardTimer.running))
            return
        linkCard.close()
        linkCard.url = ""
        linkCard.pending = url
        linkCard.anchor = view.mapFromItem(null, scenePos.x, scenePos.y)
        linkCardTimer.restart()
    }
    function hideLinkCard() {
        linkCardTimer.stop()
        linkCard.close()
        linkCard.url = ""
        linkCard.pending = ""
    }
    // A medium named in a note's text, shown while the pointer is on its name
    function showMediaPreview(url, scenePos) {
        mediaPreview.source = url
        const p = view.mapFromItem(null, scenePos.x, scenePos.y)
        mediaPreview.x = Math.max(8, Math.min(p.x + 12, view.width - mediaPreview.width - 8))
        mediaPreview.y = Math.max(8, Math.min(p.y + 18, view.height - mediaPreview.height - 8))
        mediaPreview.open()
    }
    function hideMediaPreview() { mediaPreview.close() }
    function showBacklinks(id, anchor) {
        backlinkMenu.entries = Search.backlinks(Board.projectPath, id)
        backlinkMenu.popup(anchor, 0, anchor.height)
    }

    // --- Pictures of the canvas --------------------------------------------------

    // mode: "copy" (clipboard), "png" or "pdf" (to fileUrl). No ids: everything.
    // What an export covers, in canvas coordinates: around the items (the
    // whole canvas or the selection), or what is on screen ("view")
    function visibleIds() {
        const ids = []
        for (let i = 0; i < Board.count; ++i) {
            const it = Board.get(i)
            if (!it.hidden)
                ids.push(it.id)
        }
        return ids
    }
    function exportArea(area) {
        if (area === "view") {
            const a = toWorld(0, 0), b = toWorld(width, height)
            return Qt.rect(a.x, a.y, b.x - a.x, b.y - a.y)
        }
        const ids = area === "selection" && selection.length > 0 ? selection : visibleIds()
        const bounds = Board.boundsOf(ids)
        const margin = 32
        return Qt.rect(bounds.x - margin, bounds.y - margin, bounds.width + 2 * margin, bounds.height + 2 * margin)
    }
    // The scale an export renders at: as asked, unless that would be too
    // large a picture
    function exportScale(rect, scale) {
        return Math.min(scale, 8000 / Math.max(1, rect.width, rect.height))
    }
    // `options`: {area: "all", "selection" or "view", scale (2), privacy:
    // the text as grey bars}
    function exportImage(ids, mode, fileUrl, options) {
        options = options || {}
        const area = options.area || (ids && ids.length > 0 ? "selection" : "all")
        if (area === "view" || !ids || ids.length === 0)
            ids = visibleIds()
        if (ids.length === 0) {
            toast.show(qsTr("Nothing to export"))
            return
        }
        // Sections bring their contents
        const set = {}
        for (const id of ids) {
            set[id] = true
            for (const inner of Board.contentsOf(id))
                set[inner] = true
        }
        exportSet = set
        exporter.ids = Object.keys(set)
        exporter.mode = mode
        exporter.fileUrl = fileUrl || ""
        exporter.area = area === "view" ? exportArea("view") : Qt.rect(0, 0, 0, 0)
        exporter.renderScale = options.scale || 2
        Theme.redacting = options.privacy === true
        exporting = true
        editingId = ""
        exportTimer.start()
    }
    function finishExport(result) {
        const path = Board.tempPath("canvas-export.png")
        const saved = result && result.saveToFile(path)
        exporting = false
        Theme.redacting = false
        exportSet = ({})
        if (!saved) {
            toast.show(qsTr("Could not render the picture"))
            return
        }
        if (exporter.mode === "copy") {
            Board.copyImageFile(path)
            toast.show(qsTr("Copied as a picture. Paste it into Teams, mail or a document."))
        } else if (exporter.mode === "png") {
            const target = Board.toLocalPath(exporter.fileUrl)
            if (Board.copyFile(path, target))
                toast.show(qsTr("Saved %1").arg(target))
        } else if (Board.writePdf(path, exporter.fileUrl)) {
            toast.show(qsTr("Saved %1").arg(Board.toLocalPath(exporter.fileUrl)))
        }
    }

    Timer {
        id: exportTimer
        interval: 80 // let the canvas hide what isn't exported first
        onTriggered: {
            const b = Board.boundsOf(exporter.ids)
            const margin = 32
            const area = exporter.area.width > 0 ? exporter.area
                       : Qt.rect(b.x - margin, b.y - margin, b.width + 2 * margin, b.height + 2 * margin)
            const scale = view.exportScale(area, exporter.renderScale)
            exporter.width = Math.round(area.width * scale)
            exporter.height = Math.round(area.height * scale)
            exportSource.sourceRect = area
            exportSource.textureSize = Qt.size(exporter.width, exporter.height)
            exportSource.scheduleUpdate()
            grabTimer.start()
        }
    }
    // The picture of the canvas has to be rendered once before it can be grabbed
    Timer {
        id: grabTimer
        interval: 120
        onTriggered: {
            if (!exporter.grabToImage(result => view.finishExport(result)))
                view.finishExport(null)
        }
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
            if (view.pendingFocusId !== "") {
                const id = view.pendingFocusId
                view.pendingFocusId = ""
                Qt.callLater(() => view.focusItem(id))
            }
        }
        function onProjectChanged() {
            view.clearSelection()
            view.tool = "select"
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

    readonly property var toolKeys: ({
        [Qt.Key_V]: "select", [Qt.Key_H]: "hand", [Qt.Key_N]: "note", [Qt.Key_T]: "text", [Qt.Key_K]: "code",
        [Qt.Key_R]: "rect", [Qt.Key_O]: "ellipse", [Qt.Key_D]: "diamond", [Qt.Key_S]: "section",
        [Qt.Key_L]: "connector", [Qt.Key_P]: "pen", [Qt.Key_M]: "highlighter",
    })

    Keys.onPressed: (event) => {
        if (view.isEditing)
            return
        const ctrl = event.modifiers & Qt.ControlModifier
        const shift = event.modifiers & Qt.ShiftModifier
        const alt = event.modifiers & Qt.AltModifier
        const step = shift ? 1 : 10
        if (event.key === Qt.Key_Space) {
            view.spaceHeld = true
        } else if (event.matches(StandardKey.Paste)) {
            view.pasteAt(view.insertionPoint())
        } else if (ctrl && shift && event.key === Qt.Key_C) {
            view.exportImage(view.selection, "copy")
        } else if (event.matches(StandardKey.Copy)) {
            view.copySelected()
        } else if (event.matches(StandardKey.SelectAll)) {
            view.selectAll()
        } else if (event.matches(StandardKey.Redo) || (ctrl && event.key === Qt.Key_Z && shift)) {
            Board.redo()
        } else if (event.matches(StandardKey.Undo)) {
            Board.undo()
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            view.deleteSelected()
        } else if (ctrl && event.key === Qt.Key_D) {
            view.duplicateSelected()
        } else if (event.key === Qt.Key_Tab && view.selectedId !== "") {
            view.addChild()
        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && view.selectedId !== "") {
            // In a mindmap Enter adds a sibling, elsewhere it edits
            if (Board.parentOf(view.selectedId) !== "")
                view.addSibling()
            else
                view.startEditing(view.selectedId)
        } else if (event.key === Qt.Key_F2 && view.selectedId !== "") {
            view.startEditing(view.selectedId)
        } else if (event.key === Qt.Key_Escape) {
            if (view.sizePick)
                view.finishSizePick("")
            else if (view.linkFromId !== "")
                view.linkFromId = ""
            else if (view.tool !== "select")
                view.tool = "select"
            else
                view.clearSelection()
        } else if (alt && [Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down].indexOf(event.key) >= 0) {
            view.navigate(event.key)
        } else if ([Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down].indexOf(event.key) >= 0) {
            const dx = event.key === Qt.Key_Left ? -1 : event.key === Qt.Key_Right ? 1 : 0
            const dy = event.key === Qt.Key_Up ? -1 : event.key === Qt.Key_Down ? 1 : 0
            // Nothing selected: the arrows look around instead (Shift: further)
            if (view.selection.length === 0)
                view.panBy(-dx * (shift ? 400 : 80), -dy * (shift ? 400 : 80))
            else
                view.nudge(dx * step, dy * step)
        } else if (!ctrl && !alt && (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)) {
            view.zoomBy(1.25)
        } else if (!ctrl && !alt && event.key === Qt.Key_Minus) {
            view.zoomBy(0.8)
        } else if (!ctrl && !alt && view.toolKeys[event.key] !== undefined) {
            view.tool = view.toolKeys[event.key]
        } else {
            return
        }
        event.accepted = true
    }
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat)
            view.spaceHeld = false
    }

    // --- Rendering and pointer input ----------------------------------------

    // A picture of part of the canvas, for copying and exporting; under the
    // canvas, so it is never seen
    Item {
        id: exporter
        property var ids: []
        property string mode
        property url fileUrl
        property rect area // what is on screen; empty: around the items
        property real renderScale: 2
        visible: view.exporting
        z: -10
        Rectangle {
            anchors.fill: parent
            color: Theme.canvas
        }
        ShaderEffectSource {
            id: exportSource
            anchors.fill: parent
            sourceItem: world
            live: true
            hideSource: false
        }
    }

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
        property string gesture: "" // pan, rubber, stroke, shape
        property point startWorld
        property point endWorld
        property var points: []

        cursorShape: gesture === "pan" ? Qt.ClosedHandCursor
                     : view.spaceHeld || view.tool === "hand" ? Qt.OpenHandCursor
                     : view.tool === "select" ? Qt.ArrowCursor : Qt.CrossCursor

        onEntered: view.pointerInside = true
        onExited: view.pointerInside = false

        onPressed: (mouse) => {
            view.forceActiveFocus()
            view.hideLinkPreview()
            view.hideLinkCard()
            viewAnim.stop()
            if (mouse.button === Qt.RightButton)
                return
            const p = view.toWorld(mouse.x, mouse.y)
            startWorld = p
            endWorld = p
            pressPos = Qt.point(mouse.x, mouse.y)
            if (mouse.button === Qt.MiddleButton || view.spaceHeld || view.tool === "hand") {
                gesture = "pan"
                startPanX = view.panX
                startPanY = view.panY
                return
            }
            switch (view.tool) {
            case "select": {
                const link = view.linkAt(p)
                if (link) {
                    view.selectLink(link)
                    gesture = ""
                    return
                }
                if (!(mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)))
                    view.clearSelection()
                gesture = "rubber"
                break
            }
            case "pen":
            case "highlighter":
                points = [p]
                strokePreview.path = points.slice()
                gesture = "stroke"
                break
            case "note":
            case "code":
                // Made on release: dragged out to a size, or the usual one
                gesture = "shape"
                break
            case "text":
                view.newItem("text", "", p.x, p.y - 20, 280, 44)
                view.tool = "select"
                gesture = ""
                break
            case "connector":
                gesture = ""
                break
            default: // shapes and sections: drag out their size
                gesture = "shape"
            }
        }
        onPositionChanged: (mouse) => {
            const p = view.toWorld(mouse.x, mouse.y)
            view.lastPointerWorld = p
            if (gesture === "pan") {
                view.panX = startPanX + mouse.x - pressPos.x
                view.panY = startPanY + mouse.y - pressPos.y
            } else if (gesture === "rubber" || gesture === "shape") {
                endWorld = p
            } else if (gesture === "stroke") {
                const last = points[points.length - 1]
                if (Math.hypot(p.x - last.x, p.y - last.y) * view.zoom >= 2) {
                    points.push(p)
                    strokePreview.path = points.slice()
                }
            }
        }
        onReleased: (mouse) => {
            const g = gesture
            gesture = ""
            if (g === "pan") {
                view.saveViewSoon()
            } else if (g === "rubber") {
                const r = Qt.rect(Math.min(startWorld.x, endWorld.x), Math.min(startWorld.y, endWorld.y),
                                  Math.abs(endWorld.x - startWorld.x), Math.abs(endWorld.y - startWorld.y))
                if (r.width * view.zoom > 3 || r.height * view.zoom > 3) {
                    const found = Board.itemsIn(r.x, r.y, r.width, r.height)
                    const ids = (mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) ? view.selection.slice() : []
                    for (const id of found) {
                        if (ids.indexOf(id) < 0)
                            ids.push(id)
                    }
                    view.setSelection(ids)
                }
            } else if (g === "stroke") {
                if (points.length > 1) {
                    const highlighter = view.tool === "highlighter"
                    Board.addStroke(points, highlighter ? "highlighter" : "pen",
                                    highlighter ? (view.penColor || "yellow") : view.penColor, highlighter ? 18 : 3)
                }
                points = []
                strokePreview.path = []
            } else if (g === "shape" && (view.tool === "note" || view.tool === "code")) {
                const area = Qt.rect(Math.min(startWorld.x, endWorld.x), Math.min(startWorld.y, endWorld.y),
                                     Math.abs(endWorld.x - startWorld.x), Math.abs(endWorld.y - startWorld.y))
                // A click: the usual size. Dragged: that size, at least big
                // enough to write in
                const dragged = area.width * view.zoom >= 8 || area.height * view.zoom >= 8
                if (dragged) {
                    area.width = Math.max(area.width, 120)
                    area.height = Math.max(area.height, 60)
                }
                const code = view.tool === "code"
                view.tool = "select"
                if (code)
                    view.newCodeNoteAt(startWorld, dragged ? area : undefined)
                else
                    view.newNoteAt(startWorld, undefined, dragged ? area : undefined)
            } else if (g === "shape") {
                const section = view.tool === "section"
                let w = Math.abs(endWorld.x - startWorld.x), h = Math.abs(endWorld.y - startWorld.y)
                let x = Math.min(startWorld.x, endWorld.x), y = Math.min(startWorld.y, endWorld.y)
                if (w * view.zoom < 8 && h * view.zoom < 8) { // a click: the usual size, centred
                    w = section ? 640 : 160
                    h = section ? 440 : 100
                    x = startWorld.x - w / 2
                    y = startWorld.y - h / 2
                }
                view.newItem(section ? "section" : "shape", section ? "" : view.tool, x, y, w, h)
                view.tool = "select"
            }
        }
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                const p = view.toWorld(mouse.x, mouse.y)
                const link = view.linkAt(p)
                if (link) {
                    view.selectLink(link)
                    return
                }
                canvasMenu.worldPos = p
                canvasMenu.popup()
            }
        }
        onDoubleClicked: (mouse) => {
            if (mouse.button !== Qt.LeftButton || view.tool !== "select")
                return
            const p = view.toWorld(mouse.x, mouse.y)
            const link = view.linkAt(p)
            if (link) {
                linkBar.editLabel()
                return
            }
            view.newNoteAt(p)
        }
        onWheel: (wheel) => {
            view.lastPointerWorld = view.toWorld(wheel.x, wheel.y)
            view.hideLinkPreview()
            view.hideLinkCard()
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
            // Not empty: a ShaderEffectSource renders nothing of an item
            // without a size, and exporting pictures uses one
            width: 1
            height: 1
            x: view.panX
            y: view.panY
            scale: view.zoom
            transformOrigin: Item.TopLeft

            // [[Wiki links]] between notes of this project, faint and dotted
            Repeater {
                model: Theme.prefs.showWikiLinks ? Board.wikiEdges : []
                delegate: Connector {
                    required property var modelData
                    canvas: view
                    link: modelData
                    decorative: true
                }
            }

            Repeater {
                id: linkRepeater
                model: Board.links
                delegate: Connector {
                    required property var modelData
                    canvas: view
                    link: modelData
                }
            }

            Repeater {
                model: Board
                delegate: CanvasCard {
                    canvas: view
                }
            }

            // A connector being dragged out, or the end of one being moved
            Connector {
                canvas: view
                visible: view.linkFromId !== "" && shown
                loose: true
                link: ({ from: view.linkFromId, fromSide: view.linkFromSide,
                         style: view.relinkId ? (Board.links.find(l => l.id === view.relinkId) || {}).style : "curve" })
                loosePoint: view.linkPoint
                looseTarget: view.linkTargetId ? (view.cardsVersion, view.cards[view.linkTargetId] || null) : null
                looseSide: view.linkTargetSide
                looseArrow: view.relinkEnd === "from" ? "none" : "end"
                z: 2000000
            }

            // The ends of the selected connector, to drag elsewhere
            LinkEnds {
                canvas: view
                z: 2000001
            }

            // A pen line being drawn
            Shape {
                id: strokePreviewShape
                z: 2000000
                ShapePath {
                    strokeColor: view.tool === "highlighter" ? "transparent" : Theme.penColor(view.penColor)
                    strokeWidth: 3
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    PathPolyline { id: strokePreview; path: [] }
                }
                // The highlighter as it will be: its outline, filled
                ShapePath {
                    strokeColor: "transparent"
                    fillColor: view.tool === "highlighter" ? Theme.swatch(view.penColor || "yellow") : "transparent"
                    fillRule: ShapePath.WindingFill
                    PathSvg {
                        path: view.tool === "highlighter" && strokePreview.path.length > 0
                              ? Board.strokeOutline(strokePreview.path, 18) : ""
                    }
                }
                opacity: view.tool === "highlighter" ? 0.38 : 1
            }

            // A shape or section being dragged out, and the selection rectangle
            Rectangle {
                z: 2000000
                visible: background.gesture === "rubber" || background.gesture === "shape"
                x: Math.min(background.startWorld.x, background.endWorld.x)
                y: Math.min(background.startWorld.y, background.endWorld.y)
                width: Math.abs(background.endWorld.x - background.startWorld.x)
                height: Math.abs(background.endWorld.y - background.startWorld.y)
                radius: background.gesture === "shape" && view.tool === "ellipse" ? Math.min(width, height) / 2 : 2 / view.zoom
                color: Styles.Primitives.alpha(Theme.accent, 0.08)
                border.color: Theme.accent
                border.width: 1 / view.zoom
            }

            // Smart guides
            Repeater {
                model: view.guides
                delegate: Rectangle {
                    id: guide
                    required property var modelData
                    readonly property bool measure: modelData.label !== undefined
                    readonly property bool upright: modelData.x1 === modelData.x2
                    z: 2000000
                    color: Theme.s.danger_default
                    x: Math.min(modelData.x1, modelData.x2) - (upright ? 0.5 / view.zoom : 0)
                    y: Math.min(modelData.y1, modelData.y2) - (!upright ? 0.5 / view.zoom : 0)
                    width: upright ? 1 / view.zoom : Math.abs(modelData.x2 - modelData.x1)
                    height: !upright ? 1 / view.zoom : Math.abs(modelData.y2 - modelData.y1)
                    // A measurement: ticks at its ends and the distance
                    Repeater {
                        model: guide.measure ? 2 : 0
                        delegate: Rectangle {
                            required property int index
                            color: guide.color
                            width: guide.upright ? 9 / view.zoom : 1 / view.zoom
                            height: guide.upright ? 1 / view.zoom : 9 / view.zoom
                            x: guide.upright ? (guide.width - width) / 2 : (index === 0 ? 0 : guide.width - width)
                            y: guide.upright ? (index === 0 ? 0 : guide.height - height) : (guide.height - height) / 2
                        }
                    }
                    Rectangle {
                        visible: guide.measure
                        anchors.centerIn: parent
                        scale: 1 / view.zoom
                        width: distance.implicitWidth + 8
                        height: distance.implicitHeight + 2
                        radius: 3
                        color: guide.color
                        Label {
                            id: distance
                            anchors.centerIn: parent
                            text: guide.modelData.label || ""
                            color: "#ffffff"
                            font.pixelSize: 11
                            font.weight: Styles.Primitives.fontWeight.semiBold
                        }
                    }
                }
            }

            SelectionBar {
                canvas: view
                z: 2000001
            }
            LinkBar {
                id: linkBar
                canvas: view
                z: 2000001
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
        readonly property int targetRow: {
            if (!containsDrag)
                return -1
            const p = view.toWorld(drag.x, drag.y)
            const id = Board.targetAt(p.x, p.y)
            return id && Board.getById(id).type === "note" ? Board.indexOf(id) : -1
        }

        // Everything the drop offers that the board can use: files, rich
        // text, images, web links, cells, text (see BoardModel::insertMime)
        onDropped: (drop) => {
            const pt = view.toWorld(drop.x, drop.y)
            const hit = Board.targetAt(pt.x, pt.y)
            const target = view.isEditing || !hit || Board.getById(hit).type !== "note" ? -1 : Board.indexOf(hit)
            drop.acceptProposedAction()
            const formats = {}
            for (const format of drop.formats) {
                if (/^(text\/|image\/|application\/x-(qt-image|cache-))/.test(format))
                    formats[format] = drop.getDataAsArrayBuffer(format)
            }
            if (view.isEditing) {
                const md = Board.dropMarkdown(formats)
                if (md)
                    view.insertMarkdownRequested(view.editingId, md)
                return
            }
            const ids = Board.dropData(formats, pt.x, pt.y, target)
            if (ids.length > 0)
                view.setSelection(ids)
            else
                toast.show(qsTr("Nothing here to add"))
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
            anchors.top: parent.top
            anchors.topMargin: 24
            text: view.isEditing ? qsTr("Drop to put it into the note")
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
            text: qsTr("Double-click to write a note, pick a tool below, or start from a template")
            color: Theme.textMuted
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Templates…")
            onClicked: templateDialog.open()
        }
    }

    ToolPalette {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Styles.Primitives.spacing.sp200
        canvas: view
        onTemplatesRequested: templateDialog.open()
    }

    // --- The size pipette, picking -------------------------------------------
    MouseArea {
        id: sizePicker
        anchors.fill: parent
        z: 3000
        visible: view.sizePick !== null
        hoverEnabled: true
        cursorShape: Qt.CrossCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        function sourceAt(mouse) {
            const p = view.toWorld(mouse.x, mouse.y)
            const id = Board.targetAt(p.x, p.y)
            return id !== "" && view.sizePick && view.sizePick.ids.indexOf(id) < 0 ? id : ""
        }
        onPositionChanged: (mouse) => view.sizePickHover = sourceAt(mouse)
        onExited: view.sizePickHover = ""
        // A click on an item takes its size; anywhere else, or the right
        // button, cancels
        onClicked: (mouse) => view.finishSizePick(mouse.button === Qt.LeftButton ? sourceAt(mouse) : "")
        onWheel: (wheel) => wheel.accepted = false
    }
    // The item the pointer is over, outlined
    Rectangle {
        readonly property var it: view.sizePickHover !== "" ? (Board.count, Board.getById(view.sizePickHover)) : null
        visible: it !== null && view.sizePick !== null
        z: 3001
        x: it ? view.panX + it.x * view.zoom - 3 : 0
        y: it ? view.panY + it.y * view.zoom - 3 : 0
        width: it ? it.w * view.zoom + 6 : 0
        height: it ? it.h * view.zoom + 6 : 0
        radius: Theme.radius
        color: "transparent"
        border.color: Theme.accent
        border.width: 2
    }
    // The note holding the medium the editor's media list is on
    Rectangle {
        readonly property var it: view.mediaHighlightId !== "" ? (Board.count, Board.getById(view.mediaHighlightId)) : null
        visible: it !== null && it.id !== undefined
        z: 3001
        x: it ? view.panX + it.x * view.zoom - 4 : 0
        y: it ? view.panY + it.y * view.zoom - 4 : 0
        width: it ? it.w * view.zoom + 8 : 0
        height: it ? it.h * view.zoom + 8 : 0
        radius: Theme.radius
        color: Qt.alpha(Theme.accent, 0.08)
        border.color: Theme.accent
        border.width: 3
    }

    // What the pipette does now
    Rectangle {
        visible: view.sizePick !== null
        z: 3002
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Styles.Primitives.spacing.sp150
        width: pickRow.implicitWidth + 2 * Styles.Primitives.spacing.sp100
        height: pickRow.implicitHeight + 2 * Styles.Primitives.spacing.sp075
        radius: Theme.radius * 2
        color: Theme.s.background_elevated
        border.color: Theme.accent
        Styles.Shadow {
            z: -1
            anchors.fill: parent
            radius: parent.radius
            depth: 2
        }
        Row {
            id: pickRow
            anchors.centerIn: parent
            spacing: Styles.Primitives.spacing.sp075
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "pipette"
                color: Theme.accent
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: !view.sizePick ? ""
                      : view.sizePick.what === "width" ? qsTr("Click the item whose width to take")
                      : view.sizePick.what === "height" ? qsTr("Click the item whose height to take")
                      : qsTr("Click the item whose size to take")
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Esc cancels")
                color: Theme.textMuted
                font.pixelSize: Styles.Primitives.fontSize.small
            }
        }
    }

    Minimap {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Styles.Primitives.spacing.sp200
        canvas: view
        visible: Theme.prefs.showMinimap && Board.count > 0 && !view.exporting
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 80
    }

    TemplateDialog {
        id: templateDialog
        canvas: view
    }

    // A preview of a [[linked]] note while the pointer is over the link
    Timer {
        id: previewTimer
        interval: 450
        onTriggered: {
            linkPreview.note = Search.findNote(linkPreview.target)
            linkPreview.x = Math.min(linkPreview.anchor.x + 12, view.width - linkPreview.width - 8)
            linkPreview.y = Math.min(linkPreview.anchor.y + 16, view.height - linkPreview.height - 8)
            linkPreview.open()
        }
    }
    Popup {
        id: linkPreview
        property string target
        property point anchor
        property var note: ({})
        width: 340
        height: Math.min(previewColumn.implicitHeight + 2 * padding, 320)
        padding: Styles.Primitives.spacing.sp150
        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
        focus: false

        Column {
            id: previewColumn
            width: parent.width
            spacing: Styles.Primitives.spacing.sp075
            Label {
                width: parent.width
                text: linkPreview.note.found ? linkPreview.note.title : qsTr("No note “%1” yet; click to make it").arg(linkPreview.target)
                font.weight: Styles.Primitives.fontWeight.semiBold
                elide: Text.ElideRight
            }
            Label {
                visible: linkPreview.note.found === true && linkPreview.note.current !== true
                text: qsTr("in %1").arg(linkPreview.note.projectName || "")
                color: Theme.textMuted
                font.pixelSize: Styles.Primitives.fontSize.small
            }
            Text {
                width: parent.width
                visible: linkPreview.note.found === true
                textFormat: Text.RichText
                wrapMode: Text.Wrap
                color: Theme.text
                font.pixelSize: 12
                text: !linkPreview.opened || !linkPreview.note.found ? ""
                      : Board.renderSegments(linkPreview.note.text, width, 12, Theme.renderColors)
                            .filter(p => p.kind === "text" || p.kind === "code").map(p => p.html).join("")
            }
        }
    }

    Menu {
        id: backlinkMenu
        property var entries: []
        Repeater {
            model: backlinkMenu.entries
            delegate: MenuItem {
                required property var modelData
                text: modelData.current ? modelData.title : modelData.projectName + " / " + modelData.title
                onTriggered: view.openNote(modelData.projectPath, modelData.noteId)
            }
        }
    }

    Timer {
        id: linkCardTimer
        interval: 350
        onTriggered: {
            linkCard.url = linkCard.pending
            linkCard.showIfKnown()
        }
    }
    // A link's details: a page's title, description and picture, an issue's
    // key, status and fields, a design's picture
    Popup {
        id: linkCard
        property string url     // read: the pointer rested on it
        property string pending // the pointer is on it
        property point anchor
        readonly property var info: url !== "" && Links.revision >= 0 ? Links.card(url) : ({})
        // Opens once something is known: at once, or when it comes in
        function showIfKnown() {
            if (opened || url === "" || Object.keys(info).length === 0)
                return
            x = Math.max(8, Math.min(anchor.x + 12, view.width - width - 8))
            y = Math.max(8, Math.min(anchor.y + 18, view.height - implicitHeight - 8))
            open()
        }
        onInfoChanged: showIfKnown()
        readonly property color tone: Theme.toneColor(info.tone)
        width: 360
        padding: Styles.Primitives.spacing.sp150
        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
        focus: false

        contentItem: ColumnLayout {
            spacing: Styles.Primitives.spacing.sp075
            // Its picture: a page's, or a Figma frame as Figma renders it
            Image {
                id: linkPicture
                Layout.fillWidth: true
                Layout.preferredHeight: status === Image.Ready && implicitWidth > 0
                                        ? Math.min(220, width * implicitHeight / implicitWidth) : 0
                visible: status === Image.Ready
                source: linkCard.info.image || ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                sourceSize.width: 720
            }
            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: linkCard.info.site || ""
                color: Theme.textMuted
                font.pixelSize: Styles.Primitives.fontSize.small
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Styles.Primitives.spacing.sp100
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    text: linkCard.info.title || ""
                    font.weight: Styles.Primitives.fontWeight.semiBold
                }
                // Comments still to be resolved
                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    visible: (linkCard.info.unresolved || 0) > 0
                    implicitWidth: hoverUnresolved.implicitWidth + 2 * Styles.Primitives.spacing.sp075
                    implicitHeight: hoverUnresolved.implicitHeight + 4
                    radius: Theme.radius
                    color: Qt.alpha(Theme.s.warning_default, 0.16)
                    Row {
                        id: hoverUnresolved
                        anchors.centerIn: parent
                        spacing: 4
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "message-square"
                            size: 11
                            color: Theme.s.warning_default
                        }
                        Label {
                            text: qsTr("%n unresolved", "", linkCard.info.unresolved || 0)
                            color: Theme.s.warning_default
                            font.pixelSize: Styles.Primitives.fontSize.small
                            font.weight: Styles.Primitives.fontWeight.semiBold
                        }
                    }
                }
                VoteChips {
                    Layout.alignment: Qt.AlignTop
                    badges: linkCard.info.badges || []
                }
                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    visible: (linkCard.info.status || "") !== ""
                    implicitWidth: statusLabel.implicitWidth + 2 * Styles.Primitives.spacing.sp075
                    implicitHeight: statusLabel.implicitHeight + 4
                    radius: Theme.radius
                    color: Qt.alpha(linkCard.tone, 0.16)
                    Label {
                        id: statusLabel
                        anchors.centerIn: parent
                        text: (linkCard.info.status || "").toUpperCase()
                        color: linkCard.tone
                        font.pixelSize: Styles.Primitives.fontSize.small
                        font.weight: Styles.Primitives.fontWeight.semiBold
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: linkCard.info.summary || ""
                wrapMode: Text.Wrap
            }
            // Lines of a file
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(hoverCode.implicitHeight + 12, 260)
                visible: (linkCard.info.code || "") !== ""
                radius: Theme.radius
                color: Theme.codeBackground
                clip: true
                Text {
                    id: hoverCode
                    x: 8
                    y: 6
                    textFormat: Text.RichText
                    renderType: Text.CurveRendering
                    text: (linkCard.info.code || "") === "" ? "" : Links.highlight(linkCard.info.code, linkCard.info.language || "", Theme.renderColors)
                    color: Theme.text
                    font.family: Theme.monoFont.family
                    font.pixelSize: 11
                }
            }
            // Couldn't be read: why, and where it was asked
            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: linkCard.info.error ? qsTr("Couldn't read this link. %1").arg(linkCard.info.error) : ""
                color: Theme.s.danger_default
                wrapMode: Text.Wrap
            }
            GridLayout {
                Layout.fillWidth: true
                Layout.topMargin: Styles.Primitives.spacing.sp050
                columns: 2
                columnSpacing: Styles.Primitives.spacing.sp150
                rowSpacing: Styles.Primitives.spacing.sp025
                Repeater {
                    model: (linkCard.info.fields || []).length * 2
                    delegate: Label {
                        required property int index
                        readonly property var field: linkCard.info.fields[Math.floor(index / 2)]
                        Layout.fillWidth: index % 2 === 1
                        text: index % 2 === 0 ? field.name : field.value
                        color: index % 2 === 0 ? Theme.textMuted : Theme.text
                        font.pixelSize: Styles.Primitives.fontSize.small
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // A medium named in a note: its picture (a video's still)
    Popup {
        id: mediaPreview
        property url source
        readonly property bool video: /\.(mp4|webm|mkv|mov|avi|m4v|ogv)$/i.test(source.toString())
        width: Math.min(360, view.width - 16)
        height: previewImage.status === Image.Ready && previewImage.implicitWidth > 0
                ? width * previewImage.implicitHeight / previewImage.implicitWidth + 2 * padding : 120
        padding: Styles.Primitives.spacing.sp050
        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
        focus: false
        Image {
            id: previewImage
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            sourceSize.width: 720
            source: !mediaPreview.video ? mediaPreview.source
                    : Posters.revision >= 0 ? Posters.poster(mediaPreview.source) : ""
        }
        Label {
            anchors.centerIn: parent
            visible: previewImage.status !== Image.Ready
            text: decodeURIComponent(mediaPreview.source.toString().split("/").pop())
            color: Theme.textMuted
        }
    }

    // Plays a video full size. The player is made when it opens: kept
    // around, it cost every start of the app
    Popup {
        id: videoPopup
        property url source
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 1100)
        height: Math.min(parent.height - 80, width * 9 / 16 + 48)
        modal: true
        // Takes the keys while open, so Esc closes it
        focus: true
        padding: 0
        onClosed: source = ""

        Loader {
            anchors.fill: parent
            active: videoPopup.visible && videoPopup.source.toString() !== ""
            sourceComponent: Item {
                MediaPlayer {
                    id: popupPlayer
                    source: videoPopup.source
                    videoOutput: popupOutput
                    audioOutput: AudioOutput { }
                    Component.onCompleted: play()
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
        }
    }

    FileDialog {
        id: mediaDialog
        property int targetRow: -1
        title: view.isEditing || targetRow >= 0 ? qsTr("Add files to the note") : qsTr("Add files in a new note")
        fileMode: FileDialog.OpenFiles
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        nameFilters: [
            qsTr("All files (*)"),
            qsTr("Images and videos (*.png *.jpg *.jpeg *.gif *.webp *.bmp *.svg *.mp4 *.webm *.mkv *.mov *.m4v)"),
            qsTr("Documents (*.pdf *.doc *.docx *.xls *.xlsx *.csv *.ppt *.pptx *.odt *.ods *.md *.txt)"),
        ]
        onAccepted: view.addMedia(selectedFiles, view.centerWorld(), targetRow)
    }

    Menu {
        id: canvasMenu
        property point worldPos
        MenuItem { text: qsTr("New note here"); onTriggered: view.newNoteAt(canvasMenu.worldPos) }
        MenuItem { text: qsTr("New code snippet here"); onTriggered: view.newCodeNoteAt(canvasMenu.worldPos) }
        MenuItem {
            text: qsTr("New link card here")
            onTriggered: {
                const row = Board.addItem("link", Math.round(canvasMenu.worldPos.x - 160),
                                           Math.round(canvasMenu.worldPos.y - 100), 320, 200, "", "")
                if (row >= 0)
                    view.startEditing(Board.get(row).id)
            }
        }
        KeyedMenuItem { text: qsTr("Paste here"); shortcut: "Ctrl+V"; onTriggered: { view.clearSelection(); view.pasteAt(canvasMenu.worldPos) } }
        MenuItem { text: qsTr("Insert template…"); onTriggered: templateDialog.open() }
        MenuSeparator {}
        KeyedMenuItem { text: qsTr("Select all"); shortcut: "Ctrl+A"; onTriggered: view.selectAll() }
        KeyedMenuItem { text: qsTr("Fit everything"); shortcut: "Ctrl+1"; onTriggered: view.fitAll() }
        KeyedMenuItem { text: qsTr("Fit selection"); shortcut: "Ctrl+2"; enabled: view.selection.length > 0; onTriggered: view.fitSelection() }
        KeyedMenuItem { text: qsTr("Zoom to 100%"); shortcut: "Ctrl+0"; onTriggered: view.resetZoom() }
    }

    Menu {
        id: cardMenu
        property int row: -1
        readonly property var item: row >= 0 ? Board.get(row) : ({})
        KeyedMenuItem {
            text: cardMenu.item.type === "link" ? qsTr("Change address") : qsTr("Edit")
            shortcut: "F2"
            enabled: cardMenu.item.type !== "stroke"
            onTriggered: view.startEditing(cardMenu.item.id)
        }
        MenuItem {
            visible: cardMenu.item.type === "link"
            height: visible ? implicitHeight : 0
            text: qsTr("Open in the browser")
            onTriggered: Qt.openUrlExternally(cardMenu.item.text)
        }
        MenuItem {
            visible: cardMenu.item.type === "link"
            height: visible ? implicitHeight : 0
            text: qsTr("Copy address")
            onTriggered: Board.copyText(cardMenu.item.text)
        }
        MenuItem {
            visible: cardMenu.item.type === "link"
            height: visible ? implicitHeight : 0
            text: qsTr("Pull out a detail…")
            onTriggered: view.openPullMenu(cardMenu.item.id, "right")
        }
        MenuItem {
            visible: cardMenu.item.type === "link" && Links.accounts.list.length >= 0
                     && Links.canRelate(cardMenu.item.text || "")
            height: visible ? implicitHeight : 0
            text: qsTr("Add cards of its Gerrit changes")
            onTriggered: view.addRelatedCards(cardMenu.item.id)
        }
        MenuItem {
            visible: cardMenu.item.type === "link"
            height: visible ? implicitHeight : 0
            text: qsTr("Reload")
            onTriggered: Links.reload(cardMenu.item.text)
        }
        // Every way to copy, the rich copy for Teams among them
        CopyMenu { canvas: view }
        KeyedMenuItem {
            text: view.selection.length > 1 ? qsTr("Fit selection") : qsTr("Zoom to it")
            shortcut: view.selection.length > 1 ? "Ctrl+2" : ""
            onTriggered: view.selection.length > 1 ? view.fitSelection() : view.focusItem(cardMenu.item.id)
        }
        MenuItem {
            text: cardMenu.item.locked ? qsTr("Unlock") : qsTr("Lock")
            onTriggered: Board.setLocked(view.selection.length > 0 ? view.selection : [cardMenu.item.id],
                                         !cardMenu.item.locked)
        }
        MenuItem {
            visible: cardMenu.item.type === "note" || cardMenu.item.type === "link"
            height: visible ? implicitHeight : 0
            text: view.selection.length > 1 ? qsTr("Fit the cards' height to their content")
                                             : qsTr("Fit the height to the content")
            onTriggered: view.fitToContent()
        }
        MenuSeparator {}
        KeyedMenuItem { text: qsTr("Add child"); shortcut: "Tab"; enabled: cardMenu.item.type === "note" || cardMenu.item.type === "shape"; onTriggered: view.addChild() }
        MenuItem { text: qsTr("Tidy branch"); enabled: cardMenu.item.childCount > 0; onTriggered: Board.tidy(cardMenu.item.id) }
        MenuSeparator {}
        KeyedMenuItem { text: qsTr("Duplicate"); shortcut: "Ctrl+D"; onTriggered: view.duplicateSelected() }
        MenuItem { text: qsTr("Bring to front"); onTriggered: Board.raise(cardMenu.row) }
        MenuItem { text: qsTr("Send to back"); onTriggered: Board.lower(cardMenu.row) }
        MenuItem { text: qsTr("Save as template…"); onTriggered: templateDialog.saveSelection() }
        MenuSeparator {}
        KeyedMenuItem { text: qsTr("Delete"); shortcut: "Del"; onTriggered: view.deleteSelected() }
    }
}
