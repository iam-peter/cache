import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import IgittStyle as Styles
import Cache

// The saved versions of the open project, to go back to one.
Dialog {
    id: dialog

    required property Item canvas
    property var versions: []

    function ago(ms) {
        const s = Math.round((Date.now() - ms) / 1000)
        if (s < 60) return qsTr("just now")
        if (s < 3600) return qsTr("%1 min ago").arg(Math.round(s / 60))
        if (s < 86400) return qsTr("%1 h ago").arg(Math.round(s / 3600))
        return new Date(ms).toLocaleString(Qt.locale(), Locale.ShortFormat)
    }

    title: qsTr("History of %1").arg(Workspace.currentName)
    modal: true
    closeButton: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 520
    onAboutToShow: {
        Board.save()
        versions = History.versions(Workspace.rootPath, Workspace.currentName)
    }

    ColumnLayout {
        width: parent.width
        spacing: Styles.Primitives.spacing.sp100

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textMuted
            text: !History.available ? qsTr("History needs git, which isn't installed.")
                  : !History.enabled ? qsTr("History is off; turn it on in Settings.")
                  : qsTr("A version is kept a little while after each change. Restoring one keeps the current state as a version too, so it can be undone.")
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 380)
            clip: true
            model: dialog.versions
            ScrollIndicator.vertical: ScrollIndicator { }
            delegate: ItemDelegate {
                id: version
                required property var modelData
                required property int index
                width: ListView.view.width
                icon.source: Icons.source("history")
                contentItem: RowLayout {
                    Label {
                        Layout.fillWidth: true
                        text: version.modelData.subject
                        elide: Text.ElideRight
                    }
                    Label {
                        text: dialog.ago(version.modelData.time)
                        color: Theme.textMuted
                        font.pixelSize: Styles.Primitives.fontSize.small
                    }
                    Button {
                        visible: version.index > 0
                        text: qsTr("Restore")
                        styles: [Styles.ButtonStyle.small]
                        onClicked: {
                            dialog.canvas.clearSelection()
                            Board.close()
                            const ok = History.restore(Workspace.rootPath, Workspace.currentName, version.modelData.sha)
                            Board.load(Workspace.currentPath)
                            dialog.close()
                            dialog.canvas.showMessage(ok ? qsTr("Restored the version from %1").arg(dialog.ago(version.modelData.time))
                                                         : qsTr("Could not restore that version"))
                        }
                    }
                    Label {
                        visible: version.index === 0
                        text: qsTr("current")
                        color: Theme.textMuted
                        font.pixelSize: Styles.Primitives.fontSize.small
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: list.count === 0 && History.available
                text: qsTr("No versions yet")
                color: Theme.textMuted
            }
        }
    }
}
