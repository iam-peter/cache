import QtQuick
import QtQuick.Controls
import IgittStyle as Styles
import Cache

// A link's chips beside its status: a review label's vote each, "+2",
// green for, red against; hovering one names the label and who voted.
Row {
    id: chips

    // [{name, short, value, tone, votes}] (LinkInfo::badges)
    property var badges: []

    spacing: Styles.Primitives.spacing.sp050
    visible: badges.length > 0

    Repeater {
        model: chips.badges
        delegate: Rectangle {
            id: chip
            required property var modelData
            readonly property color tone: Theme.toneColor(modelData.tone)
            implicitWidth: chipLabel.implicitWidth + 2 * Styles.Primitives.spacing.sp075
            implicitHeight: chipLabel.implicitHeight + 4
            radius: Theme.radius
            color: Qt.alpha(tone, 0.16)
            Label {
                id: chipLabel
                anchors.centerIn: parent
                text: chip.modelData.value
                color: chip.tone
                font.pixelSize: Styles.Primitives.fontSize.small
                font.weight: Styles.Primitives.fontWeight.semiBold
                font.features: { "tnum": 1 }
            }
            HoverHandler { id: chipHover }
            ToolTip.visible: chipHover.hovered
            ToolTip.delay: 400
            ToolTip.text: chip.modelData.name + ": " + (chip.modelData.votes || chip.modelData.value)
        }
    }
}
