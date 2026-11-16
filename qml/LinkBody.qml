import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import IgittStyle as Styles
import Cache

// A web link as a card: the page's picture, title, description and site,
// or an issue's key, status, summary and fields (with an account for its
// host). Double-click to change the address; Ctrl+click on the title, or
// the button beside the site, opens it.
Item {
    id: body

    required property Item card
    readonly property bool editing: card.editing
    readonly property string url: card.itemText
    // Asked again whenever an answer comes in (revision)
    readonly property var info: url !== "" && Links.revision >= 0 ? Links.card(url) : ({})
    readonly property bool known: info.title !== undefined
    readonly property color tone: info.tone === "done" ? Theme.s.success_default
                                : info.tone === "progress" ? Theme.s.info_default : Theme.textMuted
    readonly property real pad: Styles.Primitives.spacing.sp150

    function commit() {
        let text = editor.text.trim()
        if (text !== "" && !/^[a-z][a-z0-9+.-]*:\/\//i.test(text))
            text = "https://" + text
        if (text !== body.url)
            Board.setText(body.card.index, text)
    }
    onEditingChanged: {
        if (editing) {
            editor.text = body.url
            editor.selectAll()
            editor.forceActiveFocus()
        } else {
            commit()
        }
    }

    clip: true

    // The page's picture across the top, when the card has room for it
    Image {
        id: picture
        visible: !body.editing && status === Image.Ready && body.height > 150
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 1
        // A design is the point: most of the card, whole; a page's picture
        // is a banner
        readonly property bool design: body.info.kind === "design"
        height: !visible ? 0 : design ? body.height * 0.68 : Math.min(body.height * 0.5, width / 1.91)
        source: body.info.image || ""
        fillMode: design ? Image.PreserveAspectFit : Image.PreserveAspectCrop
        asynchronous: true
        sourceSize.width: 640
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: picture.visible ? picture.bottom : parent.top
        anchors.bottom: parent.bottom
        anchors.margins: body.pad
        anchors.topMargin: Styles.Primitives.spacing.sp100
        spacing: Styles.Primitives.spacing.sp050
        visible: !body.editing

        RowLayout {
            Layout.fillWidth: true
            spacing: Styles.Primitives.spacing.sp075
            Icon {
                name: "link"
                size: 12
                color: Theme.textMuted
            }
            Label {
                Layout.fillWidth: true
                text: body.info.site || (body.url === "" ? qsTr("No address yet: double-click to add one") : body.url)
                color: Theme.textMuted
                font.pixelSize: Styles.Primitives.fontSize.small
                elide: Text.ElideRight
            }
            IconButton {
                visible: body.url !== ""
                iconName: "external-link"
                Layout.preferredHeight: 22
                Layout.preferredWidth: 22
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Open in the browser (or Ctrl+click the title)")
                onClicked: Qt.openUrlExternally(body.url)
            }
            Rectangle {
                visible: (body.info.status || "") !== ""
                implicitWidth: statusLabel.implicitWidth + 2 * Styles.Primitives.spacing.sp075
                implicitHeight: statusLabel.implicitHeight + 4
                radius: Theme.radius
                color: Qt.alpha(body.tone, 0.16)
                Label {
                    id: statusLabel
                    anchors.centerIn: parent
                    text: (body.info.status || "").toUpperCase()
                    color: body.tone
                    font.pixelSize: Styles.Primitives.fontSize.small
                    font.weight: Styles.Primitives.fontWeight.semiBold
                }
            }
        }
        Label {
            id: title
            Layout.fillWidth: true
            visible: body.url !== ""
            text: body.info.title || body.url
            font.weight: Styles.Primitives.fontWeight.semiBold
            color: Theme.link
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            HoverHandler {
                acceptedModifiers: Qt.ControlModifier
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                acceptedModifiers: Qt.ControlModifier
                onTapped: Qt.openUrlExternally(body.url)
            }
        }
        Label {
            Layout.fillWidth: true
            // Its lines, or as many as the card has room for
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.maximumHeight: implicitHeight
            visible: text !== ""
            text: body.info.error ? qsTr("Couldn't read this link. %1").arg(body.info.error) : body.info.summary || ""
            color: body.info.error ? Theme.s.danger_default
                 : (body.info.status || "") !== "" ? Theme.text : Theme.textMuted
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            verticalAlignment: Text.AlignTop
            clip: true
        }
        // An issue's fields, as many as fit
        GridLayout {
            Layout.fillWidth: true
            visible: (body.info.fields || []).length > 0 && body.height > 190
            columns: 2
            columnSpacing: Styles.Primitives.spacing.sp150
            rowSpacing: 0
            Repeater {
                model: Math.min(4, (body.info.fields || []).length) * 2
                delegate: Label {
                    required property int index
                    readonly property var field: body.info.fields[Math.floor(index / 2)]
                    Layout.fillWidth: index % 2 === 1
                    text: index % 2 === 0 ? field.name : field.value
                    color: index % 2 === 0 ? Theme.textMuted : Theme.text
                    font.pixelSize: Styles.Primitives.fontSize.small
                    elide: Text.ElideRight
                }
            }
        }
        Item { Layout.fillHeight: true }
    }

    // Changing the address
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: body.pad
        visible: body.editing
        spacing: Styles.Primitives.spacing.sp075
        Label {
            text: qsTr("Address")
            color: Theme.textMuted
            font.pixelSize: Styles.Primitives.fontSize.small
        }
        TextField {
            id: editor
            Layout.fillWidth: true
            placeholderText: "https://"
            onAccepted: body.card.canvas.finishEditing()
            Keys.onEscapePressed: body.card.canvas.finishEditing()
        }
        Item { Layout.fillHeight: true }
    }
}
