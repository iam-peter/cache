pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import IgittStyle as Styles
import Cache

// Ctrl+P: everything Cache can do, the notes and sections of every
// project, the projects, and what Jira, Confluence and Figma have for what
// is typed (three letters or more, a moment after typing stops). Enter
// does or opens it; a found issue, page or design is added to the canvas
// as a link card (Shift+Enter opens it in the browser instead).
Popup {
    id: palette

    required property Item canvas
    // [{name, keys, run, enabled}] from the window: its commands
    property var commands: []
    property string query
    // What the services found for `query`, by source ("jira", …)
    property var remote: ({})
    property var entries: []

    function show() {
        open()
    }
    function words(text) {
        return text.toLowerCase().split(/\s+/).filter(w => w.length > 0)
    }
    function fits(text, ws) {
        const t = (text || "").toLowerCase()
        return ws.every(w => t.includes(w))
    }
    function refresh() {
        const ws = words(query)
        const list = []
        for (const c of commands) {
            if ((c.enabled === undefined || c.enabled) && fits(c.name, ws))
                list.push({ section: qsTr("Commands"), title: c.name, keys: c.keys || "", run: c.run })
        }
        if (ws.length > 0) {
            for (const n of Search.suggestNotes(query, 6))
                list.push({ section: qsTr("Notes"), title: n.title, detail: n.current ? "" : n.projectName,
                            note: n.insert })
            for (let i = 0; i < Board.count; ++i) {
                const it = Board.get(i)
                if (it.type === "section" && fits(it.title, ws))
                    list.push({ section: qsTr("Sections"), title: it.title, item: it.id })
            }
            for (const p of Workspace.projects()) {
                if (fits(p.name, ws)) {
                    list.push({ section: qsTr("Projects"), title: p.name, project: p.row })
                    if (p.path !== Workspace.currentPath)
                        list.push({ section: qsTr("Projects"), title: qsTr("Add a card of “%1” here").arg(p.name),
                                    board: p.name })
                }
            }
        }
        const names = { jira: "Jira", confluence: "Confluence", figma: "Figma" }
        for (const source of ["jira", "confluence", "figma"]) {
            for (const r of (remote[source] || []))
                list.push({ section: names[source], title: r.title, detail: r.detail, status: r.status || "",
                            tone: r.tone || "", url: r.url })
        }
        entries = list
        results.currentIndex = list.length > 0 ? 0 : -1
    }
    function activate(entry, browser) {
        if (!entry)
            return
        close()
        if (entry.run) {
            entry.run()
        } else if (entry.note) {
            const found = Search.findNote(entry.note)
            if (found.found)
                canvas.openNote(found.projectPath, found.noteId)
        } else if (entry.item) {
            canvas.focusItem(entry.item)
        } else if (entry.board) {
            canvas.addBoardCard(entry.board)
        } else if (entry.project !== undefined) {
            Workspace.currentIndex = entry.project
        } else if (entry.url) {
            if (browser)
                Qt.openUrlExternally(entry.url)
            else
                canvas.addLinkCard(entry.url)
        }
    }

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: 56
    width: Math.min(640, parent.width - 32)
    height: Math.min(column.implicitHeight + 2 * padding, parent.height - 112)
    padding: Styles.Primitives.spacing.sp100
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onOpened: {
        field.text = ""
        query = ""
        remote = ({})
        refresh()
        field.forceActiveFocus()
    }

    Timer {
        id: remoteTimer
        interval: 400
        onTriggered: {
            if (palette.query.trim().length >= 3)
                Links.search(palette.query.trim())
        }
    }
    Connections {
        target: Links
        function onFound(query, source, found) {
            if (query !== palette.query.trim() || !palette.opened)
                return
            const r = Object.assign({}, palette.remote)
            r[source] = found
            palette.remote = r
            palette.refresh()
        }
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        spacing: Styles.Primitives.spacing.sp075

        TextField {
            id: field
            Layout.fillWidth: true
            placeholderText: qsTr("A command, a note, a section, a project, or what to find in Jira, Confluence, Figma")
            onTextChanged: {
                palette.query = text
                palette.remote = ({})
                palette.refresh()
                remoteTimer.restart()
            }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Down) {
                    results.incrementCurrentIndex()
                    event.accepted = true
                } else if (event.key === Qt.Key_Up) {
                    results.decrementCurrentIndex()
                    event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    palette.activate(palette.entries[results.currentIndex], event.modifiers & Qt.ShiftModifier)
                    event.accepted = true
                }
            }
        }

        ListView {
            id: results
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 440)
            clip: true
            model: palette.entries
            currentIndex: -1
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            section.property: "section"
            section.delegate: Label {
                required property string section
                width: ListView.view.width
                topPadding: Styles.Primitives.spacing.sp075
                bottomPadding: Styles.Primitives.spacing.sp025
                leftPadding: Styles.Primitives.spacing.sp075
                text: section
                color: Theme.textMuted
                font.pixelSize: Styles.Primitives.fontSize.small
                font.weight: Styles.Primitives.fontWeight.semiBold
            }
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                onClicked: palette.activate(modelData, false)
                contentItem: RowLayout {
                    spacing: Styles.Primitives.spacing.sp100
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.title
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        visible: row.modelData.status !== undefined && row.modelData.status !== ""
                        readonly property color tone: row.modelData.tone === "done" ? Theme.s.success_default
                                                    : row.modelData.tone === "progress" ? Theme.s.info_default
                                                    : Theme.textMuted
                        implicitWidth: status.implicitWidth + 2 * Styles.Primitives.spacing.sp075
                        implicitHeight: status.implicitHeight + 4
                        radius: Theme.radius
                        color: Qt.alpha(tone, 0.16)
                        Label {
                            id: status
                            anchors.centerIn: parent
                            text: (row.modelData.status || "").toUpperCase()
                            color: parent.tone
                            font.pixelSize: Styles.Primitives.fontSize.small
                            font.weight: Styles.Primitives.fontWeight.semiBold
                        }
                    }
                    Label {
                        visible: text !== ""
                        Layout.maximumWidth: row.width * 0.45
                        text: row.modelData.detail || ""
                        color: Theme.textMuted
                        elide: Text.ElideRight
                        font.pixelSize: Styles.Primitives.fontSize.small
                    }
                    Label {
                        visible: text !== ""
                        text: Theme.keys(row.modelData.keys || "")
                        color: Qt.alpha(Theme.text, 0.5)
                        font.pixelSize: Styles.Primitives.fontSize.small
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: palette.query.trim().length >= 3 && Object.keys(palette.remote).length === 0
                     && Links.accounts.list.length > 0
            text: qsTr("Asking Jira, Confluence and Figma…")
            color: Theme.textMuted
            font.pixelSize: Styles.Primitives.fontSize.small
            leftPadding: Styles.Primitives.spacing.sp075
        }
    }
}
