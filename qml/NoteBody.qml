import QtQuick
import QtQuick.Controls
import IgittStyle as Styles
import NoteApp

// A Markdown document with its images and videos embedded: rendered when
// idle, the plain Markdown in an editor while editing.
Item {
    id: body

    required property Item card
    readonly property bool editing: card.editing
    readonly property bool overflowing: flick.contentHeight > flick.height + 1
    readonly property int textSize: 14

    clip: true

    function beginEdit() {
        editor.text = card.itemText
        editor.cursorPosition = editor.length
        editor.forceActiveFocus()
    }
    function commit() {
        if (editor.text !== card.itemText)
            Board.setText(card.index, editor.text)
    }
    // ![name](assets/file) on a line of its own, at the cursor. Images show
    // inline; videos get a player.
    function insertMedia(path) {
        const info = Board.mediaInfo(path)
        if (!info.rel)
            return false
        const snippet = "![" + info.name.replace(/[\[\]]/g, "") + "](" + encodeURI(info.rel) + ")"
        // A paragraph of its own, so every Markdown renderer shows it as a block
        const pos = editor.cursorPosition
        const head = editor.getText(Math.max(0, pos - 2), pos)
        const before = pos === 0 || head === "\n\n" ? "" : head.endsWith("\n") ? "\n" : "\n\n"
        const tail = editor.getText(pos, Math.min(editor.length, pos + 1))
        editor.insert(pos, before + snippet + (tail === "\n" ? "\n" : "\n\n"))
        editor.forceActiveFocus()
        commit()

        // Grow the card by what was added
        const inner = card.width - 32
        const added = info.type === "video" ? inner * 9 / 16
                                            : Math.min(info.width, inner) * info.height / Math.max(1, info.width)
        Board.setGeometry(card.index, card.x, card.y, card.width,
                          Math.min(card.height + added + 12, Math.max(card.height, 1200)))
        return true
    }
    function openLink(link) {
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
            commit()
        }
    }
    Component.onCompleted: if (editing) beginEdit()

    Connections {
        target: body.card.canvas
        ignoreUnknownSignals: true
        function onInsertMediaRequested(id, path) {
            if (id === body.card.itemId)
                body.insertMedia(path)
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
                           body.card.itemText, Math.max(40, flick.width - 2), body.textSize, Theme.link,
                           Search.terms, Theme.searchHighlight)

                delegate: Loader {
                    id: segment
                    required property var modelData
                    width: content.width
                    sourceComponent: modelData.kind === "video" ? videoSegment : textSegment

                    Component {
                        id: textSegment
                        Text {
                            text: segment.modelData.html
                            textFormat: Text.RichText
                            wrapMode: Text.Wrap
                            color: Theme.text
                            font.pixelSize: body.textSize
                            onLinkActivated: (link) => body.openLink(link)
                            HoverHandler { cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
                        }
                    }
                    Component {
                        id: videoSegment
                        VideoBlock {
                            source: segment.modelData.url
                            name: segment.modelData.name
                            onExpandRequested: body.card.canvas.playVideo(segment.modelData.url)
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
            placeholderText: qsTr("# Title\n\nWrite Markdown here. Paste or drop images and videos to embed them.")
            selectByMouse: true
            persistentSelection: true
            background: null

            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape) {
                    body.card.canvas.finishEditing()
                    event.accepted = true
                } else if (event.matches(StandardKey.Paste)) {
                    // Copied image/video files or image data go into the
                    // note; plain text falls through to the normal paste
                    const files = Board.clipboardMediaFiles()
                    if (files.length > 0) {
                        for (const f of files)
                            body.insertMedia(f)
                        event.accepted = true
                    } else if (Board.clipboardHasImage()) {
                        const path = Board.saveClipboardImage()
                        if (path)
                            body.insertMedia(path)
                        event.accepted = true
                    }
                } else if (event.key === Qt.Key_C && (event.modifiers & Qt.ControlModifier)
                           && (event.modifiers & Qt.ShiftModifier)) {
                    body.commit()
                    body.card.canvas.copyRow(body.card.index)
                    event.accepted = true
                }
            }
            onTextChanged: if (body.editing) commitTimer.restart()
        }
    }

    // Autosave while typing
    Timer {
        id: commitTimer
        interval: 900
        onTriggered: body.commit()
    }
}
