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
    readonly property color tone: Theme.toneColor(info.tone)
    readonly property real pad: Styles.Primitives.spacing.sp150

    // What it shows: the card's own choice, else its service's, else the
    // picture, description, status and first four fields
    readonly property bool compact: card.itemCompact
    readonly property bool ownChoice: card.itemFields.length > 0
    readonly property var chosen: ownChoice ? card.itemFields
                                : Links.revision >= 0 ? Links.cardFields(info.source || "") : []
    function shows(part) {
        return chosen.length === 0 || chosen.indexOf(part) >= 0
    }
    readonly property var allFields: info.fields || []
    readonly property var shownFields: {
        if (chosen.length === 0)
            return allFields.slice(0, 4)
        const picked = allFields.filter(f => chosen.indexOf(f.name) >= 0)
        // A service's choice made on another kind of its cards: none of
        // these fields in it
        return !ownChoice && picked.length === 0 ? allFields.slice(0, 4) : picked
    }
    // What can be shown, for choosing: [{key, name, value, out}] (out: can be
    // pulled out as a node)
    readonly property var parts: {
        let list = []
        if ((info.image || "") !== "")
            list.push({ key: ":picture", name: qsTr("Picture"), value: "", out: false })
        if ((info.summary || "") !== "" || (info.error || "") !== "")
            list.push({ key: ":description", name: qsTr("Description"), value: "", out: false })
        if ((info.status || "") !== "")
            list.push({ key: ":status", name: qsTr("Status"), value: info.status, out: true })
        for (const f of allFields)
            list.push({ key: f.name, name: f.name, value: f.value, out: true })
        if ((info.unresolved || 0) > 0)
            list.push({ key: ":unresolved", name: qsTr("Unresolved comments"), value: String(info.unresolved), out: true })
        return list
    }
    function isShown(key) {
        return key.startsWith(":") ? shows(key) : shownFields.some(f => f.name === key)
    }
    // Shows `key` or not: the card's own choice from now on
    function choose(key, on) {
        let list = parts.filter(p => p.key === key ? on : isShown(p.key)).map(p => p.key)
        // Shown nothing: still a choice, not "as usual"
        Board.setFields(card.itemId, list.length > 0 ? list : [":none"])
    }
    // Folded to its title line, or unfolded
    readonly property real compactHeight: Math.ceil(words.anchors.topMargin + words.anchors.bottomMargin
                                                    + headerRow.implicitHeight + words.spacing
                                                    + titleMetrics.lineSpacing + 2)
    function setCompact(on) {
        Board.setCompact(card.itemId, on, compactHeight)
    }
    function openDetails(anchor) {
        details.parent = anchor
        details.x = 0
        details.y = anchor.height + 4
        details.open()
    }
    FontMetrics {
        id: titleMetrics
        font: title.font
    }

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

    // How tall the words need to be: all but a long description, which
    // gets four lines here and the rest only when there is room
    readonly property real textNeed: Math.ceil(words.implicitHeight - summary.implicitHeight
                                               + Math.min(summary.implicitHeight, 4 * summaryMetrics.lineSpacing)
                                               + words.anchors.topMargin + words.anchors.bottomMargin)
    // The picture gets what the words leave, a page's picture as a banner
    readonly property real pictureRoom: body.height - 2 - textNeed
    // A picture's own height at this width: a design whole (not taller
    // than a little more than it is wide), a page's picture as a banner
    function pictureHeightFor(w) {
        if (!picture.loaded || picture.implicitWidth <= 0)
            return 0
        return picture.design ? Math.min(w * 1.2, w * picture.implicitHeight / picture.implicitWidth) : w / 1.91
    }
    // Exactly as tall as what it shows: smaller too
    function fitToContent() {
        if (body.compact)
            return
        const shown = pictureHeightFor(body.width - 2)
        const h = Math.max(body.card.minH, Math.ceil(textNeed + (shown >= 64 ? shown + 2 : 0)))
        if (Math.abs(h - body.card.height) > 1)
            Board.setGeometry(body.card.index, body.card.x, body.card.y, body.card.width, h)
    }

    FontMetrics {
        id: summaryMetrics
        font: summary.font
    }

    // Pixels in steps, so a little more zoom doesn't load a picture again
    function step(px) {
        for (const s of [400, 800, 1600, 3200])
            if (px <= s)
                return s
        return 4096
    }

    // The page's picture across the top, when the card has room for it
    Image {
        id: picture
        // Shown from its first load on, also while a sharper one loads
        property bool loaded: false
        onStatusChanged: {
            if (status === Image.Ready)
                loaded = true
            else if (status !== Image.Loading)
                loaded = false
        }
        visible: !body.editing && !body.compact && body.shows(":picture") && loaded && body.pictureRoom >= 64
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 1
        // A design is the point: most of the card, whole; a page's picture
        // is a banner
        readonly property bool design: body.info.kind === "design"
        height: !visible ? 0 : design ? body.pictureRoom : Math.min(body.pictureRoom, width / 1.91)
        source: body.info.image || ""
        fillMode: design ? Image.PreserveAspectFit : Image.PreserveAspectCrop
        asynchronous: true
        retainWhileLoading: true
        // How many pixels it covers on screen: the card's size, zoomed, on
        // this display
        readonly property real onScreen: (design ? fitted : width) * body.card.canvas.zoom * Screen.devicePixelRatio
        // A design's longest side as it fits in its box
        readonly property real fitted: implicitWidth > 0 && implicitHeight > 0 && height > 0
                                       ? Math.min(width / implicitWidth, height / implicitHeight)
                                         * Math.max(implicitWidth, implicitHeight)
                                       : width
        sourceSize.width: body.step(onScreen)
        sourceSize.height: design ? body.step(onScreen) : 0

        // A render (a Figma frame) shown larger than it was made: a sharper
        // one, once resizing and zooming have settled
        readonly property bool blurred: visible && body.info.renderSize > 0
                                        && onScreen > body.info.renderSize * 1.25
        onBlurredChanged: if (blurred) settle.restart()
        onOnScreenChanged: if (blurred) settle.restart()
        Timer {
            id: settle
            interval: 700
            onTriggered: if (picture.blurred) Links.sharpen(body.url, Math.round(picture.onScreen))
        }
    }

    ColumnLayout {
        id: words
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: picture.visible ? picture.bottom : parent.top
        anchors.bottom: parent.bottom
        anchors.margins: body.pad
        anchors.topMargin: Styles.Primitives.spacing.sp100
        spacing: Styles.Primitives.spacing.sp050
        visible: !body.editing

        RowLayout {
            id: headerRow
            Layout.fillWidth: true
            spacing: Styles.Primitives.spacing.sp075
            Icon {
                zoom: Theme.iconZoom // crisp however far the canvas zooms
                name: "link"
                size: 12
                color: Theme.textMuted
            }
            Label {
                Layout.fillWidth: true
                text: body.info.site || (body.url === "" ? qsTr("No address yet: double-click to add one") : body.url)
                Redacted { cover: body.card.fillColor }
                color: Theme.textMuted
                font.pixelSize: Styles.Primitives.fontSize.small
                elide: Text.ElideRight
            }
            IconButton {
                zoom: Theme.iconZoom // crisp however far the canvas zooms
                visible: body.url !== ""
                iconName: "external-link"
                Layout.preferredHeight: 22
                Layout.preferredWidth: 22
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Open in the browser (or Ctrl+click the title)")
                onClicked: Qt.openUrlExternally(body.url)
            }
            // Comments still to be resolved
            Rectangle {
                visible: (body.info.unresolved || 0) > 0 && body.shows(":unresolved")
                implicitWidth: unresolvedRow.implicitWidth + 2 * Styles.Primitives.spacing.sp075
                implicitHeight: unresolvedRow.implicitHeight + 4
                radius: Theme.radius
                color: Qt.alpha(Theme.s.warning_default, 0.16)
                ToolTip.visible: unresolvedHover.hovered
                ToolTip.delay: 500
                ToolTip.text: (body.info.unresolved || 0) === 1 ? qsTr("1 unresolved comment")
                              : qsTr("%1 unresolved comments").arg(body.info.unresolved || 0)
                HoverHandler { id: unresolvedHover }
                Row {
                    id: unresolvedRow
                    anchors.centerIn: parent
                    spacing: 4
                    Icon {
                        zoom: Theme.iconZoom // crisp however far the canvas zooms
                        anchors.verticalCenter: parent.verticalCenter
                        name: "message-square"
                        size: 11
                        color: Theme.s.warning_default
                    }
                    Label {
                        text: body.info.unresolved || 0
                        color: Theme.s.warning_default
                        font.pixelSize: Styles.Primitives.fontSize.small
                        font.weight: Styles.Primitives.fontWeight.semiBold
                    }
                }
            }
            Rectangle {
                visible: (body.info.status || "") !== "" && body.shows(":status")
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
            Redacted { cover: body.card.fillColor }
            font.weight: Styles.Primitives.fontWeight.semiBold
            color: Theme.link
            wrapMode: Text.Wrap
            maximumLineCount: body.compact ? 1 : 2
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
            id: summary
            Layout.fillWidth: true
            // Its lines, or as many as the card has room for
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.maximumHeight: implicitHeight
            visible: text !== "" && !body.compact && body.shows(":description")
            text: body.info.error ? qsTr("Couldn't read this link. %1").arg(body.info.error) : body.info.summary || ""
            Redacted { cover: body.card.fillColor }
            color: body.info.error ? Theme.s.danger_default
                 : (body.info.status || "") !== "" ? Theme.text : Theme.textMuted
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            verticalAlignment: Text.AlignTop
            clip: true
        }
        // Lines of a file: highlighted, numbered from where they start
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            implicitHeight: lines.implicitHeight + 2 * Styles.Primitives.spacing.sp050
            visible: (body.info.code || "") !== "" && !body.compact
            radius: Theme.radius
            color: Theme.codeBackground
            clip: true
            Row {
                id: lines
                x: Styles.Primitives.spacing.sp075
                y: Styles.Primitives.spacing.sp050
                spacing: Styles.Primitives.spacing.sp100
                Text {
                    text: {
                        const n = (body.info.code || "").split("\n").length
                        const first = body.info.firstLine || 1
                        let numbers = []
                        for (let i = 0; i < n; ++i)
                            numbers.push(first + i)
                        return numbers.join("\n")
                    }
                    color: Theme.textMuted
                    horizontalAlignment: Text.AlignRight
                    font.family: Theme.monoFont.family
                    font.pixelSize: 12
                    lineHeight: 1.0
                }
                Text {
                    textFormat: Text.RichText
                    renderType: Text.CurveRendering
                    text: (body.info.code || "") === "" ? "" : Links.highlight(body.info.code, body.info.language || "", Theme.renderColors)
                    color: Theme.text
                    font.family: Theme.monoFont.family
                    font.pixelSize: 12
                    Redacted { cover: Theme.codeBackground }
                }
            }
        }
        // An issue's fields, those chosen
        GridLayout {
            Layout.fillWidth: true
            visible: body.shownFields.length > 0 && !body.compact
            columns: 2
            columnSpacing: Styles.Primitives.spacing.sp150
            rowSpacing: 0
            Repeater {
                model: body.shownFields.length * 2
                delegate: Label {
                    required property int index
                    readonly property var field: body.shownFields[Math.floor(index / 2)]
                    Layout.fillWidth: index % 2 === 1
                    text: !field ? "" : index % 2 === 0 ? field.name : field.value
                    Redacted { cover: body.card.fillColor }
                    color: index % 2 === 0 ? Theme.textMuted : Theme.text
                    font.pixelSize: Styles.Primitives.fontSize.small
                    elide: Text.ElideRight
                }
            }
        }
        Item {
            visible: (body.info.code || "") === ""
            Layout.fillHeight: true
        }
    }

    // Choosing what it shows, folding it, pulling details out as nodes
    Popup {
        id: details
        readonly property var services: ({ jira: "Jira", confluence: "Confluence", figma: "Figma", gitlab: "GitLab",
                                           github: "GitHub", gerrit: "Gerrit", wikipedia: "Wikipedia",
                                           page: qsTr("web page") })
        readonly property string service: services[body.info.source || ""] || ""
        padding: Styles.Primitives.spacing.sp150
        width: 300
        ColumnLayout {
            width: parent.width
            spacing: Styles.Primitives.spacing.sp050
            Switch {
                Layout.fillWidth: true
                text: qsTr("Folded to its title")
                checked: body.compact
                onToggled: body.setCompact(checked)
            }
            Label {
                Layout.topMargin: Styles.Primitives.spacing.sp075
                visible: body.parts.length > 0
                text: qsTr("Shown on the card")
                color: Theme.textMuted
                font.pixelSize: Styles.Primitives.fontSize.small
            }
            Repeater {
                model: body.parts
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Styles.Primitives.spacing.sp075
                    CheckBox {
                        text: modelData.name
                        checked: body.isShown(modelData.key)
                        onToggled: body.choose(modelData.key, checked)
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData.value
                        color: Theme.textMuted
                        elide: Text.ElideRight
                        font.pixelSize: Styles.Primitives.fontSize.small
                    }
                    IconButton {
                        visible: modelData.out
                        iconName: "arrow-up-right"
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Pull out as a node of its own, which stays up to date")
                        onClicked: {
                            body.card.canvas.pullOut(body.card.itemId, modelData.key)
                            details.close()
                        }
                    }
                }
            }
            RowLayout {
                Layout.topMargin: Styles.Primitives.spacing.sp075
                spacing: Styles.Primitives.spacing.sp075
                Button {
                    visible: details.service !== "" && body.parts.length > 0
                    text: qsTr("For every %1 card").arg(details.service)
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Cards of %1 that haven't a choice of their own show the same").arg(details.service)
                    onClicked: {
                        Links.setCardFields(body.info.source,
                                            body.parts.filter(p => body.isShown(p.key)).map(p => p.key).concat([":none"]))
                        Board.setFields(body.card.itemId, [])
                    }
                }
                Button {
                    enabled: body.ownChoice
                    text: qsTr("As usual")
                    onClicked: Board.setFields(body.card.itemId, [])
                }
            }
        }
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
