pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCore
import IgittStyle as Styles
import Cache

// A picture of the canvas: of all of it, the selection or what is on
// screen, at a scale, and if wanted without its text (grey bars instead),
// to show a board's layout but not what it says. Copied, or saved as PNG
// or PDF.
Dialog {
    id: dialog

    required property Item canvas
    property string area: "all"
    property int factor: 2 // 1×, 2× or 3× the canvas's size
    property bool privacy: false
    // The picture's size in pixels, for the choice made
    property size pixels: Qt.size(0, 0)

    function measure() {
        const r = canvas.exportArea(area)
        const s = canvas.exportScale(r, factor)
        pixels = Qt.size(Math.round(r.width * s), Math.round(r.height * s))
    }
    function run(mode, file) {
        close()
        canvas.exportImage(area === "selection" ? canvas.selection : [], mode, file,
                           { area: area, scale: factor, privacy: privacy })
    }

    title: qsTr("Export a picture")
    modal: true
    closeButton: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 460

    onAboutToShow: {
        if (area === "selection" && canvas.selection.length === 0)
            area = "all"
        measure()
    }
    onAreaChanged: measure()
    onFactorChanged: measure()

    ColumnLayout {
        width: parent.width
        spacing: Styles.Primitives.spacing.sp100

        Label {
            text: qsTr("What")
            font.bold: true
        }
        RadioButton {
            text: qsTr("The whole canvas")
            checked: dialog.area === "all"
            onToggled: if (checked) dialog.area = "all"
        }
        RadioButton {
            enabled: dialog.canvas.selection.length > 0
            text: qsTr("The selection")
            checked: dialog.area === "selection"
            onToggled: if (checked) dialog.area = "selection"
        }
        RadioButton {
            text: qsTr("What's on screen")
            checked: dialog.area === "view"
            onToggled: if (checked) dialog.area = "view"
        }

        Label {
            Layout.topMargin: Styles.Primitives.spacing.sp050
            text: qsTr("Size")
            font.bold: true
        }
        RowLayout {
            spacing: Styles.Primitives.spacing.sp100
            ComboBox {
                model: ["1×", "2×", "3×"]
                currentIndex: dialog.factor - 1
                onActivated: (i) => dialog.factor = i + 1
            }
            Label {
                text: qsTr("%1 × %2 pixels").arg(dialog.pixels.width).arg(dialog.pixels.height)
                color: Theme.textMuted
            }
        }

        CheckBox {
            Layout.topMargin: Styles.Primitives.spacing.sp050
            Layout.fillWidth: true
            text: qsTr("Hide the text: grey bars instead, to share how a board is laid out, not what it says")
            checked: dialog.privacy
            onToggled: dialog.privacy = checked
            contentItem: Label {
                leftPadding: parent.indicator.width + parent.spacing
                text: parent.text
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignVCenter
            }
        }

        RowLayout {
            Layout.topMargin: Styles.Primitives.spacing.sp100
            Layout.alignment: Qt.AlignRight
            spacing: Styles.Primitives.spacing.sp100
            Button {
                text: qsTr("Copy")
                onClicked: dialog.run("copy", "")
            }
            Button {
                text: qsTr("Save as PNG…")
                onClicked: { saveDialog.mode = "png"; saveDialog.open() }
            }
            Button {
                text: qsTr("Save as PDF…")
                onClicked: { saveDialog.mode = "pdf"; saveDialog.open() }
            }
        }
    }

    FileDialog {
        id: saveDialog
        property string mode: "png"
        title: mode === "pdf" ? qsTr("Save the picture as PDF") : qsTr("Save the picture as PNG")
        fileMode: FileDialog.SaveFile
        defaultSuffix: mode
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        selectedFile: currentFolder + "/" + Workspace.currentName + "." + mode
        nameFilters: mode === "pdf" ? [qsTr("PDF (*.pdf)")] : [qsTr("PNG image (*.png)")]
        onAccepted: dialog.run(mode, selectedFile)
    }
}
