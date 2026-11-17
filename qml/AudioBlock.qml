import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import IgittStyle as Styles
import Cache

// An audio file attached to a note, played in it: play and pause, where
// it is and how long it lasts, a bar to seek on. The player is made when
// it is first played, so notes full of recordings cost nothing until then.
Rectangle {
    id: block

    required property var segment
    property bool wanted: false
    readonly property var player: loader.item

    function time(ms) {
        const s = Math.max(0, Math.floor(ms / 1000))
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }

    implicitHeight: row.implicitHeight + 2 * Styles.Primitives.spacing.sp075
    radius: Theme.radius
    color: Theme.s.background_secondary
    border.color: Theme.s.stroke_subtle

    Loader {
        id: loader
        active: block.wanted
        sourceComponent: MediaPlayer {
            source: block.segment.url
            audioOutput: AudioOutput {}
            Component.onCompleted: play()
        }
    }

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: Styles.Primitives.spacing.sp100
        spacing: Styles.Primitives.spacing.sp100

        // Play / pause
        Rectangle {
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            radius: 18
            color: Theme.accent
            Icon {
                zoom: Theme.iconZoom // crisp however far the canvas zooms
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: block.player && block.player.playing ? 0 : 1
                name: block.player && block.player.playing ? "pause" : "play"
                size: 16
                color: Theme.s.background_elevated
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (!block.player)
                        block.wanted = true
                    else if (block.player.playing)
                        block.player.pause()
                    else
                        block.player.play()
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Styles.Primitives.spacing.sp050
            RowLayout {
                Layout.fillWidth: true
                Icon {
                    zoom: Theme.iconZoom // crisp however far the canvas zooms
                    name: "music"
                    size: 12
                    color: Theme.textMuted
                }
                Label {
                    Layout.fillWidth: true
                    text: block.segment.name
                    elide: Text.ElideMiddle
                    Redacted { cover: block.color }
                }
                Label {
                    text: block.player && block.player.duration > 0
                          ? block.time(block.player.position) + " / " + block.time(block.player.duration)
                          : block.segment.size
                    color: Theme.textMuted
                    font.pixelSize: Styles.Primitives.fontSize.small
                    font.features: { "tnum": 1 }
                }
            }
            // Where it is; click to go there
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 4
                radius: 2
                color: Qt.alpha(Theme.text, 0.12)
                Rectangle {
                    width: block.player && block.player.duration > 0
                           ? parent.width * block.player.position / block.player.duration : 0
                    height: parent.height
                    radius: 2
                    color: Theme.accent
                }
                MouseArea {
                    anchors.fill: parent
                    anchors.topMargin: -6
                    anchors.bottomMargin: -6
                    enabled: block.player !== null && block.player.duration > 0
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: (mouse) => block.player.position = block.player.duration * mouse.x / width
                }
            }
        }
    }
}
