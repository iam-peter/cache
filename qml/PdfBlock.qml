import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Pdf
import IgittStyle as Styles
import Cache

// A PDF attached to a note: its pages, one at a time, as they print, with
// the page it is on and buttons to the one before and after; the button
// at the top opens it in the system's reader.
Rectangle {
    id: block

    required property var segment
    property int page: 0
    readonly property bool ready: doc.pageCount > 0
    readonly property size pagePoints: ready ? doc.pagePointSize(page) : Qt.size(595, 842)

    implicitHeight: column.implicitHeight + 2 * Styles.Primitives.spacing.sp075
    radius: Theme.radius
    color: Theme.s.background_secondary
    border.color: Theme.s.stroke_subtle

    PdfDocument {
        id: doc
        source: block.segment.url
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Styles.Primitives.spacing.sp075
        spacing: Styles.Primitives.spacing.sp075

        RowLayout {
            Layout.fillWidth: true
            spacing: Styles.Primitives.spacing.sp050
            Rectangle { // the type, as a badge
                Layout.preferredWidth: pdfLabel.implicitWidth + 10
                Layout.preferredHeight: 18
                radius: Theme.radius
                color: Styles.Primitives.alpha(Theme.accent, 0.15)
                Label {
                    id: pdfLabel
                    anchors.centerIn: parent
                    text: "PDF"
                    color: Theme.accent
                    font.pixelSize: 10
                    font.weight: Styles.Primitives.fontWeight.semiBold
                }
            }
            Label {
                Layout.fillWidth: true
                text: block.segment.name
                elide: Text.ElideMiddle
                Redacted { cover: block.color }
            }
            IconButton {
                visible: doc.pageCount > 1
                enabled: block.page > 0
                iconName: "chevron-left"
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                onClicked: block.page = Math.max(0, block.page - 1)
            }
            Label {
                visible: block.ready
                text: qsTr("%1 / %2").arg(block.page + 1).arg(doc.pageCount)
                color: Theme.textMuted
                font.pixelSize: Styles.Primitives.fontSize.small
                font.features: { "tnum": 1 }
            }
            IconButton {
                visible: doc.pageCount > 1
                enabled: block.page < doc.pageCount - 1
                iconName: "chevron-right"
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                onClicked: block.page = Math.min(doc.pageCount - 1, block.page + 1)
            }
            IconButton {
                iconName: "external-link"
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Open it in the system's reader")
                onClicked: Qt.openUrlExternally(block.segment.url)
            }
        }

        // The page, white as on paper, as wide as the note
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: width * block.pagePoints.height / Math.max(1, block.pagePoints.width)
            color: "#ffffff"
            border.color: Theme.s.stroke_subtle
            PdfPageImage {
                anchors.fill: parent
                anchors.margins: 1
                visible: block.ready
                document: doc
                currentFrame: block.page
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                // Sharp on this screen, at the note's width
                sourceSize.width: Math.max(1, Math.ceil(width * Screen.devicePixelRatio))
            }
            Label {
                anchors.centerIn: parent
                visible: !block.ready
                text: doc.error !== 0 ? qsTr("Can't read this PDF") : qsTr("Opening…")
                color: "#666666"
            }
        }
    }
}
