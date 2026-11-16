import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import IgittStyle as Styles
import Cache

ApplicationWindow {
    id: win

    width: 1440
    height: 900
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    title: (Workspace.currentName.length ? Workspace.currentName + " — " : "") + "Cache"
    font: Theme.uiFont

    Settings {
        category: "window"
        property alias width: win.width
        property alias height: win.height
        property alias sidebarVisible: sidebar.visible
        property var splitState
        Component.onCompleted: if (splitState) split.restoreState(splitState)
        Component.onDestruction: splitState = split.saveState()
    }

    // Plain Text items use the application font
    readonly property font uiFont: Theme.uiFont
    onUiFontChanged: Fonts.setApplicationFont(uiFont)
    Component.onCompleted: {
        Fonts.setApplicationFont(uiFont)
        Board.load(Workspace.currentPath)
    }
    onClosing: Board.save()

    // A version of the project for its history, a while after each save
    Connections {
        target: Board
        function onSaved() { History.schedule(Workspace.rootPath, Workspace.currentName) }
    }

    Connections {
        target: Workspace
        function onCurrentChanged() {
            canvasView.clearSelection()
            if (Board.projectPath !== Workspace.currentPath)
                Board.load(Workspace.currentPath)
        }
    }

    // --- Shortcuts ----------------------------------------------------------

    // While the command palette is open, its own keys
    readonly property bool popupOpen: palette.opened
    Shortcut { sequences: ["Ctrl+P"]; enabled: !win.popupOpen; onActivated: palette.show() }
    Shortcut { sequences: ["Ctrl+N"]; enabled: !win.popupOpen; onActivated: canvasView.newNote() }
    Shortcut { sequences: ["Ctrl+Shift+I"]; enabled: !win.popupOpen; onActivated: canvasView.pickMedia(canvasView.selectedRow()) }
    Shortcut { sequences: ["Ctrl+B"]; onActivated: sidebar.visible = !sidebar.visible }
    Shortcut { sequences: ["Ctrl+F"]; enabled: !win.popupOpen; onActivated: { sidebar.visible = true; sidebar.focusSearch() } }
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; onActivated: canvasView.zoomBy(1.25) }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: canvasView.zoomBy(0.8) }
    Shortcut { sequences: ["Ctrl+0"]; onActivated: canvasView.resetZoom() }
    Shortcut { sequences: ["Ctrl+1"]; onActivated: canvasView.fitAll() }
    Shortcut { sequences: ["Ctrl+2"]; onActivated: canvasView.fitSelection() }
    Shortcut { sequences: [StandardKey.Preferences, "Ctrl+,"]; onActivated: settingsDialog.open() }
    Shortcut { sequences: ["F1"]; onActivated: helpDialog.open() }
    Shortcut { sequences: ["Ctrl+Shift+T"]; enabled: !win.popupOpen; onActivated: templates.open() }
    Shortcut { sequences: ["Ctrl+Shift+H"]; enabled: !win.popupOpen; onActivated: historyDialog.open() }

    // --- Layout -------------------------------------------------------------

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6

            IconButton {
                styles: [Styles.ButtonStyle.medium]
                iconName: "panel-left"
                checkable: true
                checked: sidebar.visible
                onClicked: sidebar.visible = checked
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Show or hide the sidebar (Ctrl+B)")
            }
            Label {
                Layout.leftMargin: Styles.Primitives.spacing.sp100
                Layout.maximumWidth: 280
                text: Workspace.currentName
                elide: Text.ElideRight
                font.weight: Styles.Primitives.fontWeight.semiBold
            }

            Item { Layout.fillWidth: true }

            IconButton {
                styles: [Styles.ButtonStyle.medium]
                iconName: "sticky-note"
                text: qsTr("New note")
                onClicked: canvasView.newNote()
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("New note (Ctrl+N, or double-click the canvas)")
            }
            IconButton {
                styles: [Styles.ButtonStyle.medium]
                iconName: "image"
                text: qsTr("Add files…")
                onClicked: canvasView.pickMedia(canvasView.selectedRow())
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: canvasView.isEditing ? qsTr("Insert images, videos or files at the cursor (Ctrl+Shift+I)")
                            : canvasView.selectedId !== "" ? qsTr("Add images, videos or files to the selected note (Ctrl+Shift+I)")
                            : qsTr("Add images, videos or files in a new note (Ctrl+Shift+I)")
            }
            IconButton {
                styles: [Styles.ButtonStyle.medium]
                iconName: "copy"
                text: qsTr("Copy note")
                enabled: canvasView.selectedId !== ""
                onClicked: canvasView.copySelected()
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Copy the selected note with its images, to paste into Teams, mail or a document (Ctrl+C)")
            }

            ToolSeparator {}

            IconButton {
                styles: [Styles.ButtonStyle.medium]
                iconName: "undo-2"
                enabled: Board.canUndo
                onClicked: Board.undo()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Undo (Ctrl+Z)")
            }
            IconButton {
                styles: [Styles.ButtonStyle.medium]
                iconName: "redo-2"
                enabled: Board.canRedo
                onClicked: Board.redo()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Redo (Ctrl+Shift+Z)")
            }

            ToolSeparator {}

            IconButton {
                styles: [Styles.ButtonStyle.medium]
                iconName: "zoom-out"
                onClicked: canvasView.zoomBy(0.8)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Zoom out (Ctrl+-)")
            }
            ToolButton {
                id: zoomButton
                styles: [Styles.ButtonStyle.medium]
                text: Math.round(canvasView.zoom * 100) + "%"
                // As wide as the widest percentage, so it never elides and
                // the buttons beside it don't move while zooming
                Layout.preferredWidth: Math.ceil(zoomWidest.advanceWidth) + leftPadding + rightPadding
                TextMetrics {
                    id: zoomWidest
                    font: zoomButton.font
                    text: "888%"
                }
                onClicked: canvasView.resetZoom()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Zoom to 100% (Ctrl+0)")
            }
            IconButton {
                styles: [Styles.ButtonStyle.medium]
                iconName: "zoom-in"
                onClicked: canvasView.zoomBy(1.25)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Zoom in (Ctrl++)")
            }
            IconButton {
                styles: [Styles.ButtonStyle.medium]
                iconName: "maximize"
                onClicked: canvasView.fitAll()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Fit everything (Ctrl+1)")
            }
            IconButton {
                styles: [Styles.ButtonStyle.medium]
                iconName: "fit-selection"
                enabled: canvasView.selection.length > 0
                onClicked: canvasView.fitSelection()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Fit selection (Ctrl+2)")
            }

            ToolSeparator {}

            IconButton {
                styles: [Styles.ButtonStyle.medium]
                iconName: "ellipsis"
                onClicked: moreMenu.popup()
                ToolTip.visible: hovered && !moreMenu.visible
                ToolTip.text: qsTr("More")
                Menu {
                    id: moreMenu
                    MenuItem {
                        text: qsTr("Export canvas as Markdown")
                        onTriggered: {
                            const path = Board.exportMarkdown()
                            if (path) {
                                canvasView.showMessage(qsTr("Exported to export.md"))
                                Qt.openUrlExternally("file://" + path)
                            }
                        }
                    }
                    MenuItem { text: qsTr("Copy canvas as picture"); onTriggered: canvasView.exportImage([], "copy") }
                    MenuItem {
                        text: qsTr("Export a picture…")
                        onTriggered: {
                            exportDialog.area = canvasView.selection.length > 1 ? "selection" : "all"
                            exportDialog.open()
                        }
                    }
                    MenuSeparator {}
                    KeyedMenuItem { text: qsTr("Templates…"); shortcut: "Ctrl+Shift+T"; onTriggered: templates.open() }
                    KeyedMenuItem {
                        text: qsTr("History…")
                        shortcut: "Ctrl+Shift+H"
                        enabled: History.available
                        onTriggered: historyDialog.open()
                    }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Open project folder"); onTriggered: Qt.openUrlExternally(Board.projectUrl) }
                    MenuItem { text: qsTr("Change workspace folder…"); onTriggered: sidebar.changeWorkspace() }
                    MenuSeparator {}
                    KeyedMenuItem { text: qsTr("Keyboard shortcuts"); shortcut: "F1"; onTriggered: helpDialog.open() }
                }
            }
            IconButton {
                styles: [Styles.ButtonStyle.medium]
                iconName: "settings"
                onClicked: settingsDialog.open()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Settings (Ctrl+,)")
            }
        }
    }

    SplitView {
        id: split
        anchors.fill: parent
        orientation: Qt.Horizontal
        handle: SplitterHandle {}

        Sidebar {
            id: sidebar
            canvas: canvasView
            SplitView.preferredWidth: 270
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: 480
        }

        CanvasView {
            id: canvasView
            SplitView.fillWidth: true
        }
    }

    SettingsDialog { id: settingsDialog }
    HistoryDialog { id: historyDialog; canvas: canvasView }
    TemplateDialog { id: templates; canvas: canvasView }

    ExportDialog { id: exportDialog; canvas: canvasView }

    CommandPalette {
        id: palette
        canvas: canvasView
        readonly property var selection: canvasView.selection
        readonly property bool locked: selection.length > 0 && selection.every(id => Board.getById(id).locked)
        commands: [
            { name: qsTr("New note"), keys: "Ctrl+N", run: () => canvasView.newNote() },
            { name: qsTr("New code snippet"), run: () => canvasView.newCodeNoteAt(canvasView.centerWorld()) },
            { name: qsTr("New link card"), run: () => canvasView.newLinkCard() },
            { name: qsTr("Add images, videos or files…"), keys: "Ctrl+Shift+I",
              run: () => canvasView.pickMedia(canvasView.selectedRow()) },
            { name: qsTr("Search all projects"), keys: "Ctrl+F",
              run: () => { sidebar.visible = true; sidebar.focusSearch() } },
            { name: qsTr("Fit everything"), keys: "Ctrl+1", run: () => canvasView.fitAll() },
            { name: qsTr("Fit selection"), keys: "Ctrl+2", enabled: selection.length > 0,
              run: () => canvasView.fitSelection() },
            { name: qsTr("Zoom to 100%"), keys: "Ctrl+0", run: () => canvasView.resetZoom() },
            { name: qsTr("Select all"), keys: "Ctrl+A", run: () => canvasView.selectAll() },
            { name: locked ? qsTr("Unlock the selection") : qsTr("Lock the selection"), enabled: selection.length > 0,
              run: () => Board.setLocked(selection, !locked) },
            { name: qsTr("Fit the notes' height to their content"), enabled: selection.length > 0,
              run: () => canvasView.fitToContent() },
            { name: qsTr("Arrange in a row"), enabled: selection.length > 1, run: () => Board.arrange(selection, "row") },
            { name: qsTr("Arrange in a column"), enabled: selection.length > 1,
              run: () => Board.arrange(selection, "column") },
            { name: qsTr("Arrange in a grid"), enabled: selection.length > 1, run: () => Board.arrange(selection, "grid") },
            { name: qsTr("Duplicate"), keys: "Ctrl+D", enabled: selection.length > 0,
              run: () => canvasView.duplicateSelected() },
            { name: qsTr("Delete"), keys: "Del", enabled: selection.length > 0 || canvasView.selectedLinkId !== "",
              run: () => canvasView.deleteSelected() },
            { name: qsTr("Go to where the connector comes from"), enabled: canvasView.selectedLinkId !== "",
              run: () => canvasView.followLink(canvasView.selectedLinkId, false) },
            { name: qsTr("Go to where the connector points"), enabled: canvasView.selectedLinkId !== "",
              run: () => canvasView.followLink(canvasView.selectedLinkId, true) },
            { name: qsTr("Export a picture…"), run: () => exportDialog.open() },
            { name: qsTr("Copy canvas as picture"), run: () => canvasView.exportImage([], "copy") },
            { name: qsTr("Export canvas as Markdown"), run: () => {
                const path = Board.exportMarkdown()
                if (path)
                    Qt.openUrlExternally("file://" + path)
            } },
            { name: qsTr("Templates…"), keys: "Ctrl+Shift+T", run: () => templates.open() },
            { name: qsTr("History…"), keys: "Ctrl+Shift+H", enabled: History.available, run: () => historyDialog.open() },
            { name: qsTr("Show or hide the sidebar"), keys: "Ctrl+B", run: () => sidebar.visible = !sidebar.visible },
            { name: qsTr("Settings"), keys: "Ctrl+,", run: () => settingsDialog.open() },
            { name: qsTr("Keyboard shortcuts"), keys: "F1", run: () => helpDialog.open() },
        ]
    }

    Dialog {
        id: helpDialog
        title: qsTr("Keyboard shortcuts")
        modal: true
        closeButton: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 520

        ColumnLayout {
            width: parent.width
            spacing: Styles.Primitives.spacing.sp075

            Repeater {
                model: [
                    ["Double-click canvas", qsTr("New note")],
                    ["V  H", qsTr("Select / pan tool (or hold Space to pan)")],
                    ["N  T  K", qsTr("Note, text and code snippet tools")],
                    ["R  O  D  S", qsTr("Rectangle, ellipse, diamond, section")],
                    ["L  P  M", qsTr("Connector, pen, highlighter")],
                    ["Drag on empty space", qsTr("Select several (Shift adds)")],
                    ["Ctrl+A", qsTr("Select all")],
                    ["Tab / Enter", qsTr("Mindmap: add a child / a sibling")],
                    ["Alt+arrows", qsTr("Mindmap: go to parent, child, siblings")],
                    ["Enter / F2", qsTr("Edit the selected item")],
                    ["Esc", qsTr("Finish editing / back to select / deselect")],
                    ["[[", qsTr("While editing: link another note")],
                    ["Ctrl+Shift+K", qsTr("While editing: code block")],
                    ["Ctrl+C", qsTr("Copy (a note: with its images, for Teams)")],
                    ["Ctrl+Shift+C", qsTr("Copy the selection as a picture")],
                    ["Ctrl+V", qsTr("Paste items, images, videos or text")],
                    ["Ctrl+D", qsTr("Duplicate")],
                    ["Del", qsTr("Delete items or a connector")],
                    ["Arrows (+Shift)", qsTr("Nudge 10 px (1 px); with nothing selected, pan (further)")],
                    ["Alt while dragging", qsTr("Drag a copy")],
                    ["Shift while dragging", qsTr("Move along one direction")],
                    ["Space while dragging or resizing", qsTr("Without snapping")],
                    ["Shift while resizing", qsTr("Keep the proportions")],
                    ["Alt while resizing", qsTr("Resize around the middle")],
                    ["Middle button / Space+drag", qsTr("Pan")],
                    ["Wheel / Shift+wheel", qsTr("Zoom (or scroll, as set) / scroll sideways")],
                    ["Ctrl+wheel, + / −", qsTr("Zoom")],
                    ["Ctrl+Z / Ctrl+Shift+Z", qsTr("Undo / redo")],
                    ["Ctrl+0 / Ctrl+1 / Ctrl+2", qsTr("Zoom 100% / fit everything / fit selection")],
                    ["Ctrl+P", qsTr("Command palette: commands, notes, projects, Jira, Confluence, Figma")],
                    ["Ctrl+F", qsTr("Search all projects")],
                    ["Ctrl+Shift+T / H", qsTr("Templates / history")],
                    ["Ctrl+B", qsTr("Show or hide the sidebar")],
                    ["Ctrl+,", qsTr("Settings")],
                ]
                delegate: RowLayout {
                    id: shortcutRow
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Styles.Primitives.spacing.sp200
                    Label {
                        Layout.preferredWidth: 190
                        text: shortcutRow.modelData[0]
                        font: Theme.monoFont
                        color: Theme.link
                    }
                    Label {
                        Layout.fillWidth: true
                        text: shortcutRow.modelData[1]
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
