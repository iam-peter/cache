import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import IgittStyle as Styles
import NoteApp

// Projects of the workspace on top, the notes on the current canvas below.
Pane {
    id: sidebar

    required property Item canvas

    function focusSearch() {
        search.forceActiveFocus()
        search.selectAll()
    }
    function newProject() {
        nameDialog.row = -1
        nameField.text = ""
        nameDialog.open()
    }
    function changeWorkspace() { folderDialog.open() }

    padding: 0

    component SectionLabel: Label {
        font.pixelSize: Styles.Primitives.fontSize.small
        font.weight: Styles.Primitives.fontWeight.semiBold
        color: Theme.textMuted
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Projects ---------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Styles.Primitives.spacing.sp150
            Layout.rightMargin: Styles.Primitives.spacing.sp075
            Layout.topMargin: Styles.Primitives.spacing.sp100
            SectionLabel { text: qsTr("Projects"); Layout.fillWidth: true }
            IconButton {
                iconName: "plus"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("New project")
                onClicked: sidebar.newProject()
            }
        }

        ListView {
            id: projectList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, sidebar.height * 0.38)
            Layout.topMargin: Styles.Primitives.spacing.sp050
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: Workspace
            ScrollIndicator.vertical: ScrollIndicator { }

            delegate: ItemDelegate {
                id: projectRow
                required property int index
                required property string name
                width: ListView.view.width
                text: name
                icon.source: Icons.source(highlighted ? "folder-open" : "folder")
                highlighted: index === Workspace.currentIndex
                onClicked: {
                    sidebar.canvas.clearSelection()
                    Workspace.currentIndex = index
                }
                onDoubleClicked: {
                    nameDialog.row = index
                    nameField.text = name
                    nameDialog.open()
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        projectMenu.row = projectRow.index
                        projectMenu.name = projectRow.name
                        projectMenu.popup()
                    }
                }
            }
        }

        MenuSeparator { Layout.fillWidth: true }

        // --- Notes on this canvas -----------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Styles.Primitives.spacing.sp150
            Layout.rightMargin: Styles.Primitives.spacing.sp150
            SectionLabel { text: qsTr("Notes"); Layout.fillWidth: true }
            Label {
                text: documentList.count === Board.count ? Board.count : documentList.count + " / " + Board.count
                color: Theme.textMuted
                font.pixelSize: Styles.Primitives.fontSize.small
            }
        }

        TextField {
            id: search
            Layout.fillWidth: true
            Layout.margins: Styles.Primitives.spacing.sp100
            placeholderText: qsTr("Search notes  (Ctrl+F)")
            Keys.onEscapePressed: {
                text = ""
                sidebar.canvas.forceActiveFocus()
            }
            Keys.onReturnPressed: if (documentList.count > 0) documentList.itemAtIndex(0).clicked()
        }

        ListView {
            id: documentList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator { }
            model: DocumentFilterModel {
                sourceModel: Board
                filterText: search.text
            }

            delegate: ItemDelegate {
                required property string itemId
                required property string title
                required property string itemText
                width: ListView.view.width
                text: title
                icon.source: Icons.source(/!\[[^\]]*\]\([^)]*\.(mp4|webm|mkv|mov|m4v)\)/i.test(itemText) ? "film"
                                          : itemText.indexOf("![") >= 0 ? "image" : "sticky-note")
                highlighted: sidebar.canvas.selectedId === itemId
                onClicked: sidebar.canvas.focusItem(itemId)
            }

            Label {
                anchors.centerIn: parent
                visible: documentList.count === 0
                text: search.text.length ? qsTr("No matches") : qsTr("Nothing here yet")
                color: Theme.textMuted
            }
        }

        MenuSeparator { Layout.fillWidth: true }

        // --- Workspace folder ---------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Styles.Primitives.spacing.sp150
            Layout.rightMargin: Styles.Primitives.spacing.sp075
            Layout.bottomMargin: Styles.Primitives.spacing.sp050
            Label {
                Layout.fillWidth: true
                text: Workspace.rootPath
                elide: Text.ElideMiddle
                color: Theme.textMuted
                font.pixelSize: Styles.Primitives.fontSize.small
                ToolTip.visible: footerHover.hovered
                ToolTip.text: Workspace.rootPath
                HoverHandler { id: footerHover }
            }
            IconButton {
                iconName: "folder-open"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Change the workspace folder")
                onClicked: sidebar.changeWorkspace()
            }
        }
    }

    Menu {
        id: projectMenu
        property int row: -1
        property string name
        MenuItem {
            text: qsTr("Rename…")
            onTriggered: {
                nameDialog.row = projectMenu.row
                nameField.text = projectMenu.name
                nameDialog.open()
            }
        }
        MenuItem {
            text: qsTr("Show in file manager")
            onTriggered: Qt.openUrlExternally(Workspace.projectUrl(projectMenu.row))
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Move to trash…")
            onTriggered: {
                deleteDialog.row = projectMenu.row
                deleteDialog.name = projectMenu.name
                deleteDialog.open()
            }
        }
    }

    Dialog {
        id: nameDialog
        property int row: -1
        title: row < 0 ? qsTr("New project") : qsTr("Rename project")
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 380
        standardButtons: Dialog.Ok | Dialog.Cancel

        TextField {
            id: nameField
            width: parent.width
            placeholderText: qsTr("Project name")
            onAccepted: nameDialog.accept()
        }

        onOpened: {
            nameField.forceActiveFocus()
            nameField.selectAll()
        }
        onAccepted: {
            const name = nameField.text.trim()
            if (!name)
                return
            sidebar.canvas.clearSelection()
            if (row < 0) {
                const created = Workspace.createProject(name)
                if (created >= 0)
                    Workspace.currentIndex = created
                return
            }
            const wasCurrent = row === Workspace.currentIndex
            if (wasCurrent)
                Board.close() // save and release the folder before renaming it
            if (!Workspace.renameProject(row, name))
                sidebar.canvas.showMessage(qsTr("Could not rename to “%1”").arg(name))
            if (wasCurrent && Board.projectPath !== Workspace.currentPath)
                Board.load(Workspace.currentPath)
        }
    }

    Dialog {
        id: deleteDialog
        property int row: -1
        property string name
        title: qsTr("Move project to trash?")
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 400
        standardButtons: Dialog.Yes | Dialog.Cancel

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("“%1” and everything in it will be moved to the system trash.").arg(deleteDialog.name)
        }

        onAccepted: {
            const wasCurrent = row === Workspace.currentIndex
            if (wasCurrent) {
                sidebar.canvas.clearSelection()
                Board.close()
            }
            if (!Workspace.removeProject(row))
                sidebar.canvas.showMessage(qsTr("Could not move “%1” to the trash").arg(name))
            if (Board.projectPath !== Workspace.currentPath)
                Board.load(Workspace.currentPath)
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose the workspace folder")
        currentFolder: "file://" + Workspace.rootPath
        onAccepted: {
            sidebar.canvas.clearSelection()
            Board.close()
            Workspace.setRootPath(selectedFolder)
            if (Board.projectPath !== Workspace.currentPath)
                Board.load(Workspace.currentPath)
        }
    }
}
