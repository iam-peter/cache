import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import IgittStyle as Styles
import NoteApp

ApplicationWindow {
    id: win

    width: 1440
    height: 900
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    title: (Workspace.currentName.length ? Workspace.currentName + " — " : "") + "Note Canvas"
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

    Connections {
        target: Workspace
        function onCurrentChanged() {
            canvasView.clearSelection()
            if (Board.projectPath !== Workspace.currentPath)
                Board.load(Workspace.currentPath)
        }
    }

    // --- Shortcuts ----------------------------------------------------------

    Shortcut { sequences: ["Ctrl+N"]; enabled: !win.popupOpen; onActivated: canvasView.newNote() }
    Shortcut { sequences: ["Ctrl+Shift+I"]; enabled: !win.popupOpen; onActivated: canvasView.pickMedia(canvasView.selectedRow()) }
    Shortcut { sequences: ["Ctrl+B"]; onActivated: sidebar.visible = !sidebar.visible }
    Shortcut { sequences: ["Ctrl+F"]; enabled: !win.popupOpen; onActivated: { sidebar.visible = true; sidebar.focusSearch() } }
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; onActivated: canvasView.zoomBy(1.25) }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: canvasView.zoomBy(0.8) }
    Shortcut { sequences: ["Ctrl+0"]; onActivated: canvasView.resetZoom() }
    Shortcut { sequences: ["Ctrl+1"]; onActivated: canvasView.fitAll() }
    Shortcut { sequences: [StandardKey.Preferences, "Ctrl+,"]; onActivated: settingsDialog.open() }
    Shortcut { sequences: ["F1"]; onActivated: helpDialog.open() }

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
                text: qsTr("Image / video…")
                onClicked: canvasView.pickMedia(canvasView.selectedRow())
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: canvasView.isEditing ? qsTr("Insert images or videos at the cursor (Ctrl+Shift+I)")
                            : canvasView.selectedId !== "" ? qsTr("Add images or videos to the selected note (Ctrl+Shift+I)")
                            : qsTr("Add images or videos in a new note (Ctrl+Shift+I)")
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
                styles: [Styles.ButtonStyle.medium]
                text: Math.round(canvasView.zoom * 100) + "%"
                Layout.preferredWidth: 56
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
                    MenuItem { text: qsTr("Open project folder"); onTriggered: Qt.openUrlExternally(Board.projectUrl) }
                    MenuItem { text: qsTr("Change workspace folder…"); onTriggered: sidebar.changeWorkspace() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Keyboard shortcuts"); onTriggered: helpDialog.open() }
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
                    ["Ctrl+N", qsTr("New note")],
                    ["Double-click note / Enter", qsTr("Edit note")],
                    ["Esc", qsTr("Finish editing / deselect")],
                    ["Ctrl+V", qsTr("Paste images, videos or text (into the selected note)")],
                    ["Ctrl+C", qsTr("Copy the note with its images, for Teams or mail")],
                    ["Ctrl+Shift+C", qsTr("The same while editing")],
                    ["Ctrl+Shift+I", qsTr("Add images or videos")],
                    ["Ctrl+D", qsTr("Duplicate")],
                    ["Del", qsTr("Delete")],
                    ["Arrows (+Shift)", qsTr("Nudge 10 px (1 px)")],
                    ["Ctrl+Z / Ctrl+Shift+Z", qsTr("Undo / redo")],
                    ["Wheel / Ctrl+Wheel", qsTr("Zoom (or scroll, see Settings)")],
                    ["Drag empty space", qsTr("Pan (the middle button pans anywhere)")],
                    ["Ctrl+0 / Ctrl+1", qsTr("Zoom 100% / fit everything")],
                    ["Ctrl+F", qsTr("Search notes")],
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
