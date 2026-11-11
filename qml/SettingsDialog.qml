pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import IgittStyle as Styles
import NoteApp

// Theme, fonts and canvas behaviour. Every setting takes effect at once.
Dialog {
    id: dialog

    title: qsTr("Settings")
    modal: true
    closeButton: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 520

    ColumnLayout {
        width: parent.width
        spacing: Styles.Primitives.spacing.sp100

        Label {
            text: qsTr("Appearance")
            font.bold: true
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Styles.Primitives.spacing.sp150
            rowSpacing: Styles.Primitives.spacing.sp100

            Label { text: qsTr("Theme") }
            // The system's light or dark, then every theme by name
            ComboBox {
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "id"
                model: [{id: "system", name: qsTr("System")}].concat(Styles.Themes.available)
                currentIndex: count > 0 ? Math.max(0, indexOfValue(Styles.Themes.theme)) : -1
                onActivated: Styles.Themes.theme = currentValue
            }

            Item { width: 1; height: 1 }
            ColumnLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pixelSize: Styles.Primitives.fontSize.small
                    text: qsTr("These are igitt's themes. Your own go in %1 as JSON files and are read again whenever they change.")
                              .arg(Styles.Themes.folder)
                }
                Label {
                    Layout.fillWidth: true
                    visible: Styles.Themes.errors.length > 0
                    wrapMode: Text.Wrap
                    color: Theme.s.danger_default
                    text: Styles.Themes.errors.join("\n")
                }
                Button {
                    text: qsTr("Open the theme folder")
                    styles: [Styles.ButtonStyle.small]
                    onClicked: Qt.openUrlExternally(Styles.Themes.openFolder())
                }
            }

            Label { text: qsTr("Interface font") }
            RowLayout {
                TextField {
                    Layout.fillWidth: true
                    text: Theme.prefs.uiFamily
                    onEditingFinished: Theme.prefs.uiFamily = text
                }
                SpinBox {
                    from: 7
                    to: 20
                    value: Theme.prefs.uiSize
                    onValueModified: Theme.prefs.uiSize = value
                }
            }

            Label { text: qsTr("Editor font") }
            RowLayout {
                TextField {
                    Layout.fillWidth: true
                    text: Theme.prefs.codeFamily
                    onEditingFinished: Theme.prefs.codeFamily = text
                }
                SpinBox {
                    from: 7
                    to: 20
                    value: Theme.prefs.codeSize
                    onValueModified: Theme.prefs.codeSize = value
                }
            }
        }

        MenuSeparator { Layout.fillWidth: true }

        Label {
            text: qsTr("Canvas")
            font.bold: true
        }
        Switch {
            text: qsTr("The mouse wheel zooms (otherwise it scrolls; Ctrl+wheel always zooms)")
            checked: Theme.prefs.wheelZooms
            onToggled: Theme.prefs.wheelZooms = checked
        }
    }
}
