import QtQuick
import QtQuick.Controls
import IgittStyle as Styles
import Cache

// A Markdown document with its images, videos, code and linked notes:
// rendered when idle, the plain Markdown in an editor while editing.
Item {
    id: body

    required property Item card
    readonly property bool editing: card.editing
    readonly property bool overflowing: flick.contentHeight > flick.height + 1
    readonly property int textSize: 14

    clip: true

    function beginEdit() {
        editor.text = card.itemText
        const at = card.canvas.editCursor
        editor.cursorPosition = at >= 0 ? Math.min(at, editor.length) : editor.length
        card.canvas.editCursor = -1
        editor.forceActiveFocus()
    }
    function commit() {
        if (editor.text !== card.itemText)
            Board.setText(card.index, editor.text)
    }
    // Markdown from a drop, a paste or the file picker, at the cursor: on
    // lines of its own when it is a block (media, files, tables, several
    // lines), else inline
    function insertMarkdown(md) {
        if (/\n/.test(md) || /^(!?\[|\|)/.test(md))
            insertBlock(md)
        else
            editor.insert(editor.cursorPosition, md)
        editor.forceActiveFocus()
        commit()
    }
    // A paragraph of its own, so every Markdown renderer shows it as a block.
    // Returns where `text` starts.
    function insertBlock(text) {
        const pos = editor.cursorPosition
        const head = editor.getText(Math.max(0, pos - 2), pos)
        const before = pos === 0 || head === "\n\n" ? "" : head.endsWith("\n") ? "\n" : "\n\n"
        const tail = editor.getText(pos, Math.min(editor.length, pos + 1))
        editor.insert(pos, before + text + (tail === "\n" ? "\n" : "\n\n"))
        editor.forceActiveFocus()
        return pos + before.length
    }
    // ``` around the selection, or an empty block with the cursor inside
    function insertCode() {
        const selected = editor.selectedText
        if (selected.length > 0) {
            const start = editor.selectionStart
            editor.remove(editor.selectionStart, editor.selectionEnd)
            editor.cursorPosition = start
        }
        const at = insertBlock("```\n" + selected + "\n```")
        editor.cursorPosition = at + 3 // right after the opening ```, for the language
    }

    // --- The editing toolbar's Markdown -----------------------------------
    // `mark` around the selection, or around `placeholder` (selected, to type
    // over) when nothing is; again on marked text takes the marks off
    function wrap(mark, placeholder) {
        const s = editor.selectionStart, e = editor.selectionEnd, n = mark.length
        const selected = editor.getText(s, e)
        if (selected.length >= 2 * n && selected.startsWith(mark) && selected.endsWith(mark)) {
            editor.remove(s, e)
            editor.insert(s, selected.slice(n, selected.length - n))
            editor.select(s, e - 2 * n)
        } else if (editor.getText(Math.max(0, s - n), s) === mark && editor.getText(e, Math.min(editor.length, e + n)) === mark) {
            editor.remove(e, e + n)
            editor.remove(s - n, s)
            editor.select(s - n, e - n)
        } else {
            // Spaces at the edges stay outside: "**word **" is not bold
            const lead = selected.match(/^\s*/)[0], trail = selected.trim().length > 0 ? selected.match(/\s*$/)[0] : ""
            const core = selected.trim().length > 0 ? selected.slice(lead.length, selected.length - trail.length) : ""
            const text = core.length > 0 ? core : placeholder
            const at = s + (core.length > 0 ? lead.length : 0)
            editor.remove(s, e)
            editor.insert(s, (core.length > 0 ? lead : selected) + mark + text + mark + trail)
            editor.select(at + n, at + n + text.length)
        }
        editor.forceActiveFocus()
    }
    // The whole lines the selection (or the cursor) is on
    function lineSpan() {
        const text = editor.text
        const s = editor.selectionStart, e = editor.selectionEnd
        const start = s === 0 ? 0 : text.lastIndexOf("\n", s - 1) + 1
        // A selection that ends at the start of a line leaves that line out
        const last = e > s ? e - 1 : e
        const end = text.indexOf("\n", last)
        return { start: start, end: end < 0 ? text.length : end }
    }
    // A heading, list, task list or quote for the selected lines; again on
    // lines that already are one turns them back into text
    function setLines(kind) {
        const span = lineSpan()
        const lines = editor.getText(span.start, span.end).split("\n")
        const heading = kind.startsWith("h")
        const marks = { h1: "# ", h2: "## ", h3: "### ", bullet: "- ", task: "- [ ] ", quote: "> " }
        const prefixFor = (i) => kind === "number" ? (i + 1) + ". " : marks[kind]
        // What a line starts with: its indent, any heading, list, task or
        // quote mark, and the text after it
        const parse = (line) => {
            const m = line.match(/^(\s*)(#{1,6}\s+|>\s?|(?:[-*+]|\d+[.)])\s+(?:\[[ xX]\]\s+)?)?(.*)$/)
            return { indent: m[1], mark: m[2] || "", text: m[3] }
        }
        const isKind = (p) => kind === "number" ? /^\d+[.)]\s+$/.test(p.mark)
                            : kind === "bullet" ? /^[-*+]\s+$/.test(p.mark)
                            : kind === "task" ? /\[[ xX]\]\s+$/.test(p.mark)
                            : p.mark.trim() === marks[kind].trim()
        const parsed = lines.map(parse)
        const used = parsed.filter(p => (p.mark + p.text).trim().length > 0)
        const off = used.length > 0 && used.every(isKind)
        let i = 0
        const changed = parsed.map((p, n) => {
            if (lines.length > 1 && (p.mark + p.text).trim().length === 0)
                return lines[n]
            // Headings and quotes start at the margin; lists keep their indent
            const indent = heading || kind === "quote" ? "" : p.indent
            return off ? indent + p.text : indent + prefixFor(i++) + p.text
        }).join("\n")
        const single = lines.length === 1 && editor.selectionStart === editor.selectionEnd
        editor.remove(span.start, span.end)
        editor.insert(span.start, changed)
        if (single)
            editor.cursorPosition = span.start + changed.length
        else
            editor.select(span.start, span.start + changed.length)
        editor.forceActiveFocus()
    }
    function insertLink() {
        const s = editor.selectionStart, e = editor.selectionEnd
        const selected = editor.getText(s, e)
        const isUrl = /^https?:\/\/\S+$/.test(selected)
        const label = isUrl || selected.length === 0 ? qsTr("text") : selected
        const target = isUrl ? selected : "https://"
        editor.remove(s, e)
        editor.insert(s, "[" + label + "](" + target + ")")
        if (isUrl || selected.length === 0)
            editor.select(s + 1, s + 1 + label.length)
        else
            editor.select(s + label.length + 3, s + label.length + 3 + target.length)
        editor.forceActiveFocus()
    }
    function format(action) {
        switch (action) {
        case "bold": wrap("**", qsTr("bold")); break
        case "italic": wrap("*", qsTr("italic")); break
        case "strike": wrap("~~", qsTr("text")); break
        case "inline-code": wrap("`", qsTr("code")); break
        case "link": insertLink(); break
        case "codeblock": insertCode(); break
        case "table": {
            const at = insertBlock("| " + qsTr("Column") + " | " + qsTr("Column") + " |\n|---|---|\n|  |  |")
            editor.select(at + 2, at + 2 + qsTr("Column").length)
            break
        }
        case "rule": editor.cursorPosition = insertBlock("---") + 3; break
        default: setLines(action) // h1, h2, h3, bullet, number, task, quote
        }
    }

    function openLink(link) {
        if (link.startsWith("task:")) {
            Board.toggleTask(card.index, parseInt(link.substring(5)))
            return
        }
        if (link.startsWith("note:")) {
            card.canvas.openWikiLink(decodeURIComponent(link.substring(5)), card.itemId)
            return
        }
        const url = /^[a-z][a-z0-9+.-]+:/i.test(link) ? link : Board.projectUrl + "/" + link
        if (/\.(mp4|webm|mkv|mov|avi|m4v|ogv)$/i.test(link))
            card.canvas.playVideo(url)
        else
            Qt.openUrlExternally(url)
    }

    onEditingChanged: {
        if (editing) {
            beginEdit()
        } else {
            commitTimer.stop()
            suggestions.close()
            commit()
            growTimer.start()
        }
    }
    // After editing, a note grows to show what was written (up to a point)
    Timer {
        id: growTimer
        interval: 60
        onTriggered: {
            const needed = flick.contentHeight + Styles.Primitives.spacing.sp200 + Styles.Primitives.spacing.sp150 + 8
            if (needed > body.card.height + 2)
                Board.setGeometry(body.card.index, body.card.x, body.card.y, body.card.width,
                                  Math.min(needed, Math.max(body.card.height, 720)))
        }
    }
    Component.onCompleted: if (editing) beginEdit()

    Connections {
        target: body.card.canvas
        ignoreUnknownSignals: true
        function onInsertMarkdownRequested(id, markdown) {
            if (id === body.card.itemId)
                body.insertMarkdown(markdown)
        }
        function onInsertCodeRequested(id) {
            if (id === body.card.itemId)
                body.insertCode()
        }
        function onFormatRequested(id, action) {
            if (id === body.card.itemId && body.editing)
                body.format(action)
        }
    }

    // A web image or link fetched in the background: into the editor too
    Connections {
        target: Board
        enabled: body.editing
        function onTextReplaced(before, after) {
            const at = editor.text.indexOf(before)
            if (at < 0)
                return
            const cursor = editor.cursorPosition
            editor.text = editor.text.split(before).join(after)
            editor.cursorPosition = cursor > at ? Math.min(editor.length, cursor + after.length - before.length) : cursor
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: Styles.Primitives.spacing.sp200
        anchors.topMargin: Styles.Primitives.spacing.sp150
        visible: !body.editing
        interactive: false
        clip: true
        contentWidth: width
        contentHeight: content.implicitHeight + 4

        Column {
            id: content
            width: flick.width
            spacing: Styles.Primitives.spacing.sp100

            Repeater {
                // Search matches are highlighted while a search is on. Not
                // while editing: the editor covers it, and laying the text out
                // again at every autosave made typing stutter
                model: body.editing || body.card.itemText.trim().length === 0 ? [] : Board.renderSegments(
                           body.card.itemText, Math.max(40, flick.width - 2), body.textSize, Theme.renderColors,
                           Search.highlightWords)

                delegate: Loader {
                    id: piece
                    required property var modelData
                    width: content.width
                    sourceComponent: modelData.kind === "video" ? videoSegment
                                   : modelData.kind === "code" ? codeSegment
                                   : modelData.kind === "embed" ? embedSegment
                                   : modelData.kind === "file" ? fileSegment
                                   : textSegment

                    Component {
                        id: textSegment
                        Text {
                            id: rich
                            text: piece.modelData.html
                            textFormat: Text.RichText
                            // Distance-field text (the default) crashed Qt
                            // 6.12's renderer when a zoomed-out note with a
                            // table was resized: this Text is rebuilt at every
                            // step. Curves also stay sharp at any zoom.
                            renderType: Text.CurveRendering
                            wrapMode: Text.Wrap
                            color: Theme.text
                            font.pixelSize: body.textSize
                            onLinkActivated: (link) => body.openLink(link)
                            // Over a [[link]]: a preview of the note it names
                            onLinkHovered: (link) => {
                                if (link.startsWith("note:"))
                                    body.card.canvas.showLinkPreview(decodeURIComponent(link.substring(5)),
                                                                     linkHover.point.scenePosition)
                                else
                                    body.card.canvas.hideLinkPreview()
                            }
                            HoverHandler {
                                id: linkHover
                                cursorShape: rich.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                            }
                        }
                    }
                    Component {
                        id: videoSegment
                        VideoBlock {
                            source: piece.modelData.url
                            name: piece.modelData.name
                            onExpandRequested: body.card.canvas.playVideo(piece.modelData.url)
                        }
                    }
                    Component {
                        id: codeSegment
                        CodeBlock { segment: piece.modelData }
                    }
                    Component {
                        id: fileSegment
                        FileBlock { segment: piece.modelData }
                    }
                    Component {
                        id: embedSegment
                        EmbedBlock {
                            segment: piece.modelData
                            card: body.card
                        }
                    }
                }
            }

            Label {
                visible: body.card.itemText.trim().length === 0
                text: qsTr("Empty note. Double-click to write, or drop images and videos here.")
                width: content.width
                wrapMode: Text.Wrap
                font.italic: true
                color: Theme.textMuted
            }
        }

        ScrollIndicator.vertical: ScrollIndicator { }
    }

    // Fades the bottom edge when the note is longer than the card
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 1
        height: 30
        radius: Theme.radius
        visible: !body.editing && body.overflowing && !flick.atYEnd
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: body.card.fillColor }
        }
    }

    // Scroll long notes once selected; otherwise the wheel moves the canvas
    WheelHandler {
        enabled: !body.editing && body.card.selected && body.overflowing
        acceptedModifiers: Qt.NoModifier
        target: null
        onWheel: (event) => {
            const dy = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 2
            flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY - dy))
        }
    }

    ScrollView {
        id: editScroll
        anchors.fill: parent
        anchors.margins: Styles.Primitives.spacing.sp050
        visible: body.editing

        TextArea {
            id: editor
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.PlainText
            font: Theme.monoFont
            placeholderText: qsTr("# Title\n\nWrite Markdown here. Paste or drop images and videos to embed them. "
                                  + "[[ links to another note, ``` starts code, - [ ] a task.")
            selectByMouse: true
            persistentSelection: true
            background: null

            // [[ … : what is typed after the brackets, or null
            function linkPrefix() {
                const before = getText(Math.max(0, cursorPosition - 80), cursorPosition)
                const m = before.match(/\[\[([^\[\]|\n]*)$/)
                return m ? m[1] : null
            }
            function acceptSuggestion(title) {
                const prefix = linkPrefix()
                if (prefix === null)
                    return
                const end = cursorPosition
                remove(end - prefix.length, end)
                const closed = getText(cursorPosition, Math.min(length, cursorPosition + 2)) === "]]"
                insert(cursorPosition, title + (closed ? "" : "]]"))
                if (closed)
                    cursorPosition += 2
                suggestions.close()
            }

            // The editor's own: Ctrl+B is also the window's sidebar key
            Keys.onShortcutOverride: (event) => {
                if ((event.modifiers & Qt.ControlModifier) && !(event.modifiers & Qt.ShiftModifier)
                        && [Qt.Key_B, Qt.Key_I, Qt.Key_K, Qt.Key_E].indexOf(event.key) >= 0)
                    event.accepted = true
            }
            Keys.onPressed: (event) => {
                if (suggestions.opened) {
                    if (event.key === Qt.Key_Down) {
                        suggestionList.incrementCurrentIndex()
                        event.accepted = true
                        return
                    }
                    if (event.key === Qt.Key_Up) {
                        suggestionList.decrementCurrentIndex()
                        event.accepted = true
                        return
                    }
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Tab) {
                        const item = suggestionList.model[suggestionList.currentIndex]
                        if (item)
                            acceptSuggestion(item.insert)
                        event.accepted = true
                        return
                    }
                    if (event.key === Qt.Key_Escape) {
                        suggestions.close()
                        event.accepted = true
                        return
                    }
                }
                if (event.key === Qt.Key_Escape) {
                    body.card.canvas.finishEditing()
                    event.accepted = true
                } else if (event.matches(StandardKey.Paste)) {
                    // Files, images, rich text, links and cells become
                    // Markdown; plain text falls through to the normal paste
                    const md = Board.clipboardMarkdown()
                    if (md) {
                        body.insertMarkdown(md)
                        event.accepted = true
                    }
                } else if (event.key === Qt.Key_K && (event.modifiers & Qt.ControlModifier)
                           && (event.modifiers & Qt.ShiftModifier)) {
                    body.insertCode()
                    event.accepted = true
                } else if ((event.modifiers & Qt.ControlModifier) && !(event.modifiers & Qt.ShiftModifier)
                           && [Qt.Key_B, Qt.Key_I, Qt.Key_K, Qt.Key_E].indexOf(event.key) >= 0) {
                    body.format(event.key === Qt.Key_B ? "bold" : event.key === Qt.Key_I ? "italic"
                                : event.key === Qt.Key_K ? "link" : "inline-code")
                    event.accepted = true
                } else if (event.key === Qt.Key_C && (event.modifiers & Qt.ControlModifier)
                           && (event.modifiers & Qt.ShiftModifier)) {
                    body.commit()
                    body.card.canvas.copyRow(body.card.index)
                    event.accepted = true
                }
            }
            onTextChanged: {
                if (!body.editing)
                    return
                commitTimer.restart()
                const prefix = linkPrefix()
                if (prefix === null) {
                    suggestions.close()
                    return
                }
                suggestionList.model = Search.suggestNotes(prefix, 8)
                suggestionList.currentIndex = 0
                if (suggestionList.count > 0) {
                    const r = editor.mapToItem(Overlay.overlay, editor.cursorRectangle.x, editor.cursorRectangle.y
                                               + editor.cursorRectangle.height)
                    suggestions.x = r.x
                    suggestions.y = r.y + 4
                    suggestions.open()
                } else {
                    suggestions.close()
                }
            }
        }
    }

    // Completion of [[links]]: notes of every project, this one first
    Popup {
        id: suggestions
        parent: Overlay.overlay
        width: 320
        padding: Styles.Primitives.spacing.sp050
        focus: false
        closePolicy: Popup.CloseOnPressOutside

        ListView {
            id: suggestionList
            implicitHeight: Math.min(contentHeight, 260)
            width: parent.width
            clip: true
            highlightMoveDuration: 0
            delegate: ItemDelegate {
                id: suggestion
                required property var modelData
                required property int index
                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                text: modelData.current ? modelData.title : modelData.projectName + " / " + modelData.title
                icon.source: Icons.source("link")
                onClicked: editor.acceptSuggestion(modelData.insert)
            }
        }
    }

    // Autosave while typing
    Timer {
        id: commitTimer
        interval: 900
        onTriggered: body.commit()
    }
}
