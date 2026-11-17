import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import IgittStyle as Styles
import Cache

// One detail of a link pulled out of its card: a ticket's status, a
// change's votes or unresolved comments, kept up to date. Narrow, it is a
// traffic light, a dot in the detail's colour; wider, it names the ticket
// and says the value. Ctrl+click opens the link; double-click goes to its
// card.
Item {
    id: body

    required property Item card
    readonly property string url: card.itemText
    readonly property string key: card.itemShape
    // Asked again whenever an answer comes in (revision)
    readonly property var info: url !== "" && Links.revision >= 0 ? Links.card(url) : ({})
    readonly property bool known: info.title !== undefined
    readonly property string name: info.label || Links.issueKey(url) || info.title || url
    readonly property string fieldName: key === ":status" ? qsTr("Status")
                                      : key === ":unresolved" ? qsTr("Unresolved comments") : key
    readonly property string value: {
        if (!known)
            return ""
        if (key === ":status")
            return info.status || ""
        if (key === ":unresolved")
            return String(info.unresolved || 0)
        const field = (info.fields || []).find(f => f.name === key)
        return field ? field.value : ""
    }
    // Its colour: the status's tone; comments to resolve, or not; votes
    // against, or for
    readonly property string toneName: {
        if (!known || (info.error || "") !== "")
            return ""
        if (key === ":status")
            return info.tone || ""
        if (key === ":unresolved")
            return (info.unresolved || 0) > 0 ? "warning" : "done"
        if (/(^|, )[−-]\d/.test(value))
            return "failed"
        if (/(^|, )\+\d/.test(value))
            return "done"
        return ""
    }
    readonly property color tone: toneName === "warning" ? Theme.s.warning_default
                                : toneName !== "" ? Theme.toneColor(toneName) : Theme.textMuted
    // Too narrow for words: the dot alone
    readonly property bool light: width < 110
    readonly property real pad: Styles.Primitives.spacing.sp100

    HoverHandler { id: hover }
    CanvasTip {
        shown: hover.hovered
        delay: 400
        text: body.known ? qsTr("%1 · %2: %3").arg(body.name, body.fieldName, body.value || qsTr("none"))
                                 : qsTr("%1 · %2: not read yet").arg(body.name, body.fieldName)
    }
    TapHandler {
        acceptedModifiers: Qt.ControlModifier
        onTapped: Qt.openUrlExternally(body.url)
    }

    // A traffic light
    Rectangle {
        visible: body.light
        anchors.centerIn: parent
        width: Math.max(8, Math.min(body.width, body.height) - 2 * body.pad)
        height: width
        radius: width / 2
        color: body.toneName !== "" ? body.tone : "transparent"
        border.width: body.toneName !== "" ? 0 : 2
        border.color: Theme.textMuted
    }

    // The ticket and its value
    RowLayout {
        visible: !body.light
        anchors.fill: parent
        anchors.leftMargin: body.pad
        anchors.rightMargin: body.pad
        spacing: Styles.Primitives.spacing.sp100
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            radius: 6
            color: body.toneName !== "" ? body.tone : "transparent"
            border.width: body.toneName !== "" ? 0 : 2
            border.color: Theme.textMuted
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 0
            Label {
                Layout.fillWidth: true
                text: body.name
                elide: Text.ElideRight
                font.weight: Styles.Primitives.fontWeight.semiBold
                font.pixelSize: Styles.Primitives.fontSize.small
                color: Theme.text
                Redacted { cover: body.card.fillColor }
            }
            Label {
                Layout.fillWidth: true
                // A status says itself; other details say what they are
                text: !body.known ? "…"
                      : body.key === ":status" ? (body.value || qsTr("No status"))
                      : qsTr("%1: %2").arg(body.fieldName, body.value || "–")
                elide: Text.ElideRight
                font.pixelSize: Styles.Primitives.fontSize.small
                color: body.key === ":status" && body.toneName !== "" ? body.tone : Theme.textMuted
                Redacted { cover: body.card.fillColor }
            }
        }
    }
}
