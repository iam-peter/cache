import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import IgittStyle as Styles
import NoteApp

// A video embedded in a note, playing in place. It shows its first frame
// until played; the expand button opens it large.
Rectangle {
    id: block

    required property url source
    required property string name
    signal expandRequested()

    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    readonly property real aspect: player.metaData.value(MediaMetaData.Resolution)
                                   ? player.metaData.value(MediaMetaData.Resolution).width
                                     / Math.max(1, player.metaData.value(MediaMetaData.Resolution).height)
                                   : 16 / 9

    implicitHeight: width / aspect
    radius: Theme.radius
    color: "#000000"

    MediaPlayer {
        id: player
        source: block.source
        videoOutput: output
        audioOutput: AudioOutput { }
        // Pausing a loaded video that never played shows its first frame
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia && playbackState === MediaPlayer.StoppedState)
                                  pause()
    }

    VideoOutput {
        id: output
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
    }

    HoverHandler { id: hover }

    Label {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Styles.Primitives.spacing.sp100
        visible: !block.playing
        text: block.name
        color: "#ffffff"
        font.pixelSize: Styles.Primitives.fontSize.small
        style: Text.Outline
        styleColor: "#80000000"
    }

    Label {
        anchors.centerIn: parent
        visible: player.error !== MediaPlayer.NoError
        width: parent.width - 20
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: player.errorString
        color: "#ffffff"
    }

    // Play / pause
    Rectangle {
        anchors.centerIn: parent
        width: 48
        height: 48
        radius: 24
        color: "#99000000"
        visible: !block.playing || hover.hovered

        Icon {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: block.playing ? 0 : 2
            name: block.playing ? "pause" : "play"
            size: 22
            color: "#ffffff"
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: block.playing ? player.pause() : player.play()
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Styles.Primitives.spacing.sp075
        width: 28
        height: 28
        radius: Theme.radius
        color: "#99000000"
        visible: hover.hovered
        Icon {
            anchors.centerIn: parent
            name: "maximize"
            size: 16
            color: "#ffffff"
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                player.pause()
                block.expandRequested()
            }
        }
    }

    // Progress; click to seek
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 5
        color: "#40ffffff"
        visible: player.duration > 0 && (block.playing || hover.hovered || player.position > 0)

        Rectangle {
            width: parent.width * player.position / Math.max(1, player.duration)
            height: parent.height
            color: Theme.accent
        }
        MouseArea {
            anchors.fill: parent
            anchors.topMargin: -6
            cursorShape: Qt.PointingHandCursor
            onClicked: (mouse) => player.position = player.duration * mouse.x / width
        }
    }
}
