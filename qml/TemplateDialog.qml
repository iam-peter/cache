import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import IgittStyle as Styles
import NoteApp

// Starts a canvas or a part of one from a template: the built-in ones and
// those saved from a selection (kept in the workspace's .templates folder).
Dialog {
    id: dialog

    required property Item canvas
    property var templates: []

    // Save what is selected as a template of its own
    function saveSelection() {
        if (canvas.selection.length === 0) {
            canvas.showMessage(qsTr("Select what the template should hold first"))
            return
        }
        nameDialog.open()
    }

    title: qsTr("Templates")
    modal: true
    closeButton: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 560
    onAboutToShow: templates = Board.templates(Workspace.rootPath)

    ColumnLayout {
        width: parent.width
        spacing: Styles.Primitives.spacing.sp100

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textMuted
            text: qsTr("A template is placed in the middle of the view. Save your own from a selection with its right-click menu.")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Styles.Primitives.spacing.sp100
            rowSpacing: Styles.Primitives.spacing.sp100

            Repeater {
                model: dialog.templates
                delegate: ItemDelegate {
                    id: entry
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    icon.source: Icons.source("layout-template")
                    contentItem: Column {
                        spacing: 2
                        Label {
                            width: parent.width
                            text: entry.modelData.name + (entry.modelData.builtIn ? "" : qsTr("  (yours)"))
                            font.weight: Styles.Primitives.fontWeight.medium
                            elide: Text.ElideRight
                        }
                        Label {
                            width: parent.width
                            visible: text.length > 0
                            text: entry.modelData.description
                            color: Theme.textMuted
                            font.pixelSize: Styles.Primitives.fontSize.small
                            wrapMode: Text.Wrap
                        }
                    }
                    background: Rectangle {
                        radius: Theme.radius
                        color: entry.hovered ? Theme.s.background_hover : Theme.s.background_elevated
                        border.color: Theme.s.stroke_subtle
                    }
                    onClicked: {
                        const c = dialog.canvas.centerWorld()
                        const ids = Board.insertTemplate(entry.modelData.path, c.x, c.y)
                        dialog.close()
                        if (ids.length === 0)
                            return
                        // On top of what is there already: next to it instead
                        let b = Board.boundsOf(ids)
                        const others = []
                        let overlaps = false
                        for (let i = 0; i < Board.count; ++i) {
                            const it = Board.get(i)
                            if (ids.indexOf(it.id) >= 0 || it.hidden)
                                continue
                            others.push(it.id)
                            if (it.x < b.x + b.width && it.x + it.w > b.x && it.y < b.y + b.height && it.y + it.h > b.y)
                                overlaps = true
                        }
                        if (overlaps) {
                            const rest = Board.boundsOf(others)
                            const dx = rest.x + rest.width + 160 - b.x
                            const dy = rest.y - b.y
                            Board.moveItems(ids.map(id => {
                                const it = Board.getById(id)
                                return { id: id, x: it.x + dx, y: it.y + dy }
                            }))
                            b = Board.boundsOf(ids)
                        }
                        dialog.canvas.setSelection(ids)
                        dialog.canvas.centerOn(b.x, b.y, b.width, b.height, 1.0)
                    }
                }
            }
        }
    }

    Dialog {
        id: nameDialog
        title: qsTr("Save as template")
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 380
        standardButtons: Dialog.Save | Dialog.Cancel
        TextField {
            id: nameField
            width: parent.width
            placeholderText: qsTr("Name of the template")
            onAccepted: nameDialog.accept()
        }
        onOpened: {
            nameField.text = ""
            nameField.forceActiveFocus()
        }
        onAccepted: {
            if (Board.saveTemplate(dialog.canvas.selection, nameField.text, Workspace.rootPath))
                dialog.canvas.showMessage(qsTr("Saved the template “%1”").arg(nameField.text))
            else
                dialog.canvas.showMessage(qsTr("Could not save the template"))
        }
    }
}
