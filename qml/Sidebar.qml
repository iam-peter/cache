import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import IgittStyle as Styles
import Cache

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
    function openResult(row) {
        if (row < 0 || row >= resultList.count)
            return
        const item = resultList.itemAtIndex(row)
        if (item)
            canvas.openNote(item.projectPath, item.noteId)
    }
    function noteIcon(text) {
        return /!\[[^\]]*\]\([^)]*\.(mp4|webm|mkv|mov|m4v)\)/i.test(text) ? "film"
             : /!\[[^\]]*\]\(/.test(text) ? "image"
             : /^\s*```/m.test(text) ? "code"
             : /^\s*[-*+] \[[ xX]\]/m.test(text) ? "list-checks" : "sticky-note"
    }

    padding: 0

    component SectionLabel: Label {
        font.pixelSize: Styles.Primitives.fontSize.small
        font.weight: Styles.Primitives.fontWeight.semiBold
        color: Theme.textMuted
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Search, over every note of every project ----------------------
        TextField {
            id: search
            Layout.fillWidth: true
            Layout.margins: Styles.Primitives.spacing.sp100
            Layout.bottomMargin: Styles.Primitives.spacing.sp050
            placeholderText: qsTr("Search all projects  (Ctrl+F)")
            leftPadding: 30
            onTextChanged: Search.query = text
            Keys.onEscapePressed: {
                text = ""
                sidebar.canvas.forceActiveFocus()
            }
            Keys.onDownPressed: if (resultList.count > 0) resultList.currentIndex = Math.min(resultList.currentIndex + 1, resultList.count - 1)
            Keys.onUpPressed: if (resultList.count > 0) resultList.currentIndex = Math.max(resultList.currentIndex - 1, 0)
            Keys.onReturnPressed: sidebar.openResult(resultList.currentIndex)
            Keys.onEnterPressed: sidebar.openResult(resultList.currentIndex)

            Icon {
                anchors.left: parent.left
                anchors.leftMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                size: 14
                color: Theme.textMuted
            }
            IconButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 2
                visible: search.text.length > 0
                iconName: "x"
                focusPolicy: Qt.NoFocus
                onClicked: search.text = ""
            }
        }

        MenuSeparator { Layout.fillWidth: true; visible: !Search.active }

        // --- Projects ---------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Styles.Primitives.spacing.sp150
            Layout.rightMargin: Styles.Primitives.spacing.sp075
            visible: !Search.active
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
            visible: !Search.active
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

        MenuSeparator { Layout.fillWidth: true; visible: !Search.active }

        // --- The open project's notes by section, or every open task -------
        TabBar {
            id: tabs
            Layout.fillWidth: true
            Layout.leftMargin: Styles.Primitives.spacing.sp100
            Layout.rightMargin: Styles.Primitives.spacing.sp100
            visible: !Search.active
            TabButton { text: qsTr("Notes (%1)").arg(documentList.count) }
            TabButton {
                readonly property int open: (Search.revision, Search.openTasks().length)
                text: open > 0 ? qsTr("Open tasks (%1)").arg(open) : qsTr("Open tasks")
            }
        }

        ListView {
            id: documentList
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Styles.Primitives.spacing.sp050
            visible: !Search.active && tabs.currentIndex === 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator { }
            model: DocumentFilterModel {
                sourceModel: Board
            }

            delegate: ItemDelegate {
                id: row
                required property string itemId
                required property string itemType
                required property string title
                required property string itemText
                required property string sectionId
                readonly property bool isSection: itemType === "section"
                width: ListView.view.width
                // Notes in a section sit under its title
                leftPadding: (isSection || sectionId === "" ? 0 : 18) + Styles.Primitives.spacing.sp100
                text: title
                font.weight: isSection ? Styles.Primitives.fontWeight.semiBold : Styles.Primitives.fontWeight.regular
                icon.source: Icons.source(isSection ? "square-dashed" : sidebar.noteIcon(itemText))
                highlighted: sidebar.canvas.selection.indexOf(itemId) >= 0
                onClicked: sidebar.canvas.focusItem(itemId)
            }

            Label {
                anchors.centerIn: parent
                visible: documentList.count === 0
                text: qsTr("Nothing here yet")
                color: Theme.textMuted
            }
        }

        // Unticked "- [ ]" tasks of every project, the open one first
        ListView {
            id: taskList
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Styles.Primitives.spacing.sp050
            visible: !Search.active && tabs.currentIndex === 1
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator { }
            model: {
                if (!visible)
                    return []
                Search.revision
                const rows = []
                let project = ""
                for (const task of Search.openTasks()) {
                    if (task.projectPath !== project) {
                        project = task.projectPath
                        rows.push({ header: true, projectName: task.projectName })
                    }
                    rows.push(task)
                }
                return rows
            }
            delegate: Loader {
                id: entry
                required property var modelData
                width: ListView.view.width
                sourceComponent: modelData.header ? taskHeader : taskRow
                Component {
                    id: taskHeader
                    Label {
                        leftPadding: Styles.Primitives.spacing.sp150
                        topPadding: Styles.Primitives.spacing.sp100
                        bottomPadding: Styles.Primitives.spacing.sp025
                        text: entry.modelData.projectName
                        font.pixelSize: Styles.Primitives.fontSize.small
                        font.weight: Styles.Primitives.fontWeight.semiBold
                        color: Theme.textMuted
                    }
                }
                Component {
                    id: taskRow
                    ItemDelegate {
                        leftPadding: Styles.Primitives.spacing.sp100
                        onClicked: sidebar.canvas.openNote(entry.modelData.projectPath, entry.modelData.noteId)
                        contentItem: Row {
                            spacing: Styles.Primitives.spacing.sp075
                            CheckBox {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: false
                                onClicked: Search.toggleTask(entry.modelData.projectPath, entry.modelData.noteId,
                                                             entry.modelData.task)
                            }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: taskList.width - 64
                                Label {
                                    width: parent.width
                                    text: entry.modelData.text
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                                Label {
                                    width: parent.width
                                    text: entry.modelData.noteTitle
                                    elide: Text.ElideRight
                                    font.pixelSize: Styles.Primitives.fontSize.small
                                    color: Theme.textMuted
                                }
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 32
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: taskList.count === 0
                text: qsTr("No open tasks. Write “- [ ] something” in a note to add one.")
                color: Theme.textMuted
            }
        }

        // --- Matches from every project, the open one first ---------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Styles.Primitives.spacing.sp150
            Layout.rightMargin: Styles.Primitives.spacing.sp150
            visible: Search.active
            SectionLabel {
                text: qsTr("Results")
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
            Label {
                text: Search.count === 0 ? qsTr("none")
                      : (Search.count === 1 ? qsTr("1 note") : qsTr("%1 notes").arg(Search.count))
                        + (Search.projectCount > 1 ? qsTr(" in %1 projects").arg(Search.projectCount) : "")
                color: Theme.textMuted
                font.pixelSize: Styles.Primitives.fontSize.small
            }
        }

        ListView {
            id: resultList
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: Search.active
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator { }
            model: Search
            currentIndex: 0
            onCountChanged: currentIndex = count > 0 ? 0 : -1
            highlightMoveDuration: 0

            section.property: "projectName"
            section.delegate: Label {
                required property string section
                width: ListView.view.width
                leftPadding: Styles.Primitives.spacing.sp150
                topPadding: Styles.Primitives.spacing.sp100
                bottomPadding: Styles.Primitives.spacing.sp050
                text: section
                elide: Text.ElideRight
                font.pixelSize: Styles.Primitives.fontSize.small
                font.weight: Styles.Primitives.fontWeight.semiBold
                color: Theme.textMuted
            }

            delegate: ItemDelegate {
                id: result
                required property int index
                required property string projectPath
                required property string noteId
                required property string title
                required property string snippet
                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                onClicked: {
                    resultList.currentIndex = index
                    sidebar.openResult(index)
                }

                contentItem: Column {
                    spacing: Styles.Primitives.spacing.sp025
                    Label {
                        width: parent.width
                        text: result.title
                        elide: Text.ElideRight
                        font.weight: Styles.Primitives.fontWeight.medium
                    }
                    Label {
                        width: parent.width
                        visible: result.snippet.length > 0
                        text: result.snippet
                        textFormat: Text.StyledText
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        font.pixelSize: Styles.Primitives.fontSize.small
                        color: Theme.textMuted
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 32
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: resultList.count === 0
                text: qsTr("No note matches “%1”, not even with typos").arg(Search.terms.join("”, “"))
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
                text: Workspace.rootDisplayPath
                elide: Text.ElideMiddle
                color: Theme.textMuted
                font.pixelSize: Styles.Primitives.fontSize.small
                ToolTip.visible: footerHover.hovered
                ToolTip.text: Workspace.rootDisplayPath
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
