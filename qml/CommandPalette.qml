pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import IgittStyle as Styles
import Cache

// Ctrl+P: everything Cache can do, the notes and sections of every
// project, the projects, the cards of this canvas, and what Jira,
// Confluence, GitLab, GitHub, Gerrit and Figma have for what is typed
// (three letters or more, a moment after typing stops). Enter does or
// opens it; a found issue, page or design is added to the canvas as a
// link card (Shift+Enter opens it in the browser instead).
//
// #words narrow it: "#figma button" asks Figma alone, "#notes #sections
// plan" looks in notes and sections only. Typing # offers them; Tab or
// Enter takes the first.
Popup {
    id: palette

    required property Item canvas
    // [{name, keys, run, enabled}] from the window: its commands
    property var commands: []
    property string query
    // What the services found for `query`, by source ("jira", …)
    property var remote: ({})
    property var entries: []

    // What can be narrowed to: Cache's own lists, then the services (with
    // the account provider each needs)
    readonly property var scopes: [
        { key: "commands", hint: qsTr("Cache's commands") },
        { key: "notes", hint: qsTr("Notes of every project") },
        { key: "sections", hint: qsTr("Sections of this canvas") },
        { key: "cards", hint: qsTr("Link and board cards of this canvas") },
        { key: "projects", hint: qsTr("Projects") },
        { key: "jira", hint: qsTr("Jira issues"), provider: "jira" },
        { key: "confluence", hint: qsTr("Confluence pages"), provider: "jira" },
        { key: "gitlab", hint: qsTr("GitLab projects, merge requests and issues"), provider: "gitlab" },
        { key: "github", hint: qsTr("GitHub repositories, pull requests and issues"), provider: "github" },
        { key: "gerrit", hint: qsTr("Gerrit changes"), provider: "gerrit" },
        { key: "figma", hint: qsTr("Figma files of your teams"), provider: "figma" }
    ]
    readonly property var services: ["jira", "confluence", "gitlab", "github", "gerrit", "figma"]
    function hasAccount(provider) {
        return Links.accounts.list.some(a => a.provider === provider)
    }
    // The query taken apart: the scopes named (#figma), the #word being
    // typed (`partial`, null if none), and the rest, to look for
    function parse(text) {
        const tokens = text.split(/\s+/).filter(t => t.length > 0)
        const known = scopes.map(s => s.key)
        const typing = text.length > 0 && !/\s$/.test(text) && tokens.length > 0 && tokens[tokens.length - 1].startsWith("#")
        let chosen = [], rest = [], partial = null
        tokens.forEach((t, i) => {
            const key = t.startsWith("#") ? t.slice(1).toLowerCase() : null
            if (key !== null && known.indexOf(key) >= 0) {
                if (chosen.indexOf(key) < 0)
                    chosen.push(key)
            } else if (key !== null && typing && i === tokens.length - 1) {
                partial = key
            } else {
                rest.push(t)
            }
        })
        // A #word being typed that is a scope already: offered still
        if (typing && partial === null)
            partial = tokens[tokens.length - 1].slice(1).toLowerCase()
        return { scopes: chosen, partial: partial, text: rest.join(" ") }
    }
    readonly property var parsed: parse(query)
    // The services to ask: those named, or all of them
    readonly property var asked: parsed.scopes.length === 0 ? services
                                 : services.filter(s => parsed.scopes.indexOf(s) >= 0)
    function wants(key) {
        return parsed.scopes.length === 0 || parsed.scopes.indexOf(key) >= 0
    }
    // The #word being typed, completed to `key`
    function complete(key) {
        const t = field.text.replace(/#\S*$/, "")
        field.text = t + "#" + key + " "
        field.cursorPosition = field.text.length
    }

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
        const p = parsed
        const ws = words(p.text)
        // Narrowed: its lists in full, even before anything is typed
        const listAll = p.scopes.length > 0
        const list = []
        // The #word being typed: what it can become
        if (p.partial !== null) {
            for (const s of scopes) {
                if (s.key.startsWith(p.partial) && p.scopes.indexOf(s.key) < 0
                        && (!s.provider || hasAccount(s.provider)))
                    list.push({ section: qsTr("Narrow to"), title: "#" + s.key, detail: s.hint, scope: s.key })
            }
        }
        if (wants("commands")) {
            for (const c of commands) {
                if ((c.enabled === undefined || c.enabled) && fits(c.name, ws))
                    list.push({ section: qsTr("Commands"), title: c.name, keys: c.keys || "", run: c.run, query: c.query })
            }
        }
        if (ws.length > 0 || listAll) {
            if (wants("notes")) {
                for (const n of Search.suggestNotes(p.text, listAll ? 30 : 6))
                    list.push({ section: qsTr("Notes"), title: n.title, detail: n.current ? "" : n.projectName,
                                note: n.insert })
            }
            for (let i = 0; i < Board.count; ++i) {
                const it = Board.get(i)
                if (it.type === "section" && wants("sections") && fits(it.title, ws))
                    list.push({ section: qsTr("Sections"), title: it.title, item: it.id })
                // Cards by what they show, or their address
                if ((it.type === "link" || it.type === "board") && wants("cards") && (ws.length > 0 || p.scopes.indexOf("cards") >= 0)) {
                    const info = it.type === "link" && it.text !== "" ? Links.card(it.text) : ({})
                    const title = it.type === "board" ? it.text : (info.title || it.text)
                    if (fits(title + " " + (info.label || "") + " " + it.text, ws))
                        list.push({ section: qsTr("Cards"), title: title,
                                    detail: it.type === "board" ? qsTr("Project") : (info.site || ""),
                                    status: info.status || "", tone: info.tone || "", item: it.id })
                }
            }
            if (wants("projects")) {
                for (const pr of Workspace.projects()) {
                    if (fits(pr.name, ws)) {
                        list.push({ section: qsTr("Projects"), title: pr.name, project: pr.row })
                        if (pr.path !== Workspace.currentPath)
                            list.push({ section: qsTr("Projects"), title: qsTr("Add a card of “%1” here").arg(pr.name),
                                        board: pr.name })
                    }
                }
            }
        }
        const names = { jira: "Jira", confluence: "Confluence", gitlab: "GitLab", github: "GitHub", gerrit: "Gerrit",
                        figma: "Figma" }
        for (const source of asked) {
            for (const r of (remote[source] || []))
                list.push({ section: names[source], title: r.title, detail: r.detail, status: r.status || "",
                            tone: r.tone || "", url: r.url, error: r.error === true })
        }
        entries = list
        results.currentIndex = list.length > 0 ? 0 : -1
    }
    function activate(entry, browser) {
        if (!entry || entry.error)
            return
        if (entry.scope) {
            complete(entry.scope)
            return
        }
        if (entry.query) {
            // A search to show here: the palette stays
            field.text = entry.query
            return
        }
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
            const text = palette.parsed.text.trim()
            if (text.length >= 3 && palette.asked.length > 0)
                Links.search(text, palette.parsed.scopes.length > 0 ? palette.asked : [])
        }
    }
    Connections {
        target: Links
        function onFound(query, source, found) {
            if (query !== palette.parsed.text.trim() || !palette.opened)
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
            placeholderText: qsTr("A command, note, section, card or project, or what to find in your services; # narrows: #figma, #notes…")
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
                } else if (event.key === Qt.Key_Tab) {
                    // The first scope offered for the #word being typed
                    const first = palette.entries.find(e => e.scope)
                    if (first)
                        palette.complete(first.scope)
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
                        // Why a service found nothing: to read, not to pick
                        color: row.modelData.error ? Theme.s.danger_default : Theme.text
                        wrapMode: row.modelData.error ? Text.Wrap : Text.NoWrap
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        visible: row.modelData.status !== undefined && row.modelData.status !== ""
                        readonly property color tone: Theme.toneColor(row.modelData.tone)
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
            readonly property var names: ({ jira: "Jira", confluence: "Confluence", gitlab: "GitLab", github: "GitHub",
                                             gerrit: "Gerrit", figma: "Figma" })
            // The services asked that have an account
            readonly property var asking: palette.asked.filter(s => palette.hasAccount(palette.scopes.find(x => x.key === s).provider))
            visible: palette.parsed.text.trim().length >= 3 && Object.keys(palette.remote).length === 0 && asking.length > 0
            text: qsTr("Asking %1…").arg(asking.map(s => names[s]).join(", "))
            color: Theme.textMuted
            font.pixelSize: Styles.Primitives.fontSize.small
            leftPadding: Styles.Primitives.spacing.sp075
        }
    }
}
