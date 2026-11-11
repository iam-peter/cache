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
                // Search matches are highlighted while a search is on
                model: body.card.itemText.trim().length === 0 ? [] : Board.renderSegments(
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
