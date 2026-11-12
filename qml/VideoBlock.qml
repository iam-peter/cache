import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import IgittStyle as Styles
import Cache

// A video embedded in a note. It shows a still of its first frame; a
// player is loaded only while the pointer is over it or it plays, so
// panning, zooming and resizing past videos is as quick as past images.
// The expand button opens it large.
Rectangle {
    id: block

    required property url source
    required property string name
    signal expandRequested()

    readonly property url poster: (Posters.revision, Posters.poster(block.source))
    readonly property Item live: liveLoader.item
    readonly property bool playing: live !== null && live.playing
    // Play was pressed before the player was there (a touch screen, no hover)
    property bool wantPlay: false
    // The player wants to stay: playing, or paused part way. Set by the
    // player itself (a binding to it would loop through the Loader)
    property bool keepLive: false

    // The poster's shape (its size as loaded), the video's once it plays
    readonly property real aspect: posterImage.status === Image.Ready && posterImage.implicitHeight > 0
                                   ? posterImage.implicitWidth / posterImage.implicitHeight
                                   : live !== null && live.aspect > 0 ? live.aspect : 16 / 9

    implicitHeight: width / aspect
    radius: Theme.radius
    color: "#000000"

    HoverHandler { id: hover }

    Image {
        id: posterImage
        anchors.fill: parent
        source: block.poster
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        // The width it is shown at, on this screen (posters are at most
        // 960 wide)
        sourceSize.width: Math.min(960, Math.ceil(width * Screen.devicePixelRatio))
    }

    Loader {
        id: liveLoader
        anchors.fill: parent
        active: hover.hovered || block.wantPlay || block.keepLive
        sourceComponent: Item {
            readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
            // Paused part way: keep the place while the pointer is away
            readonly property bool keep: playing || (player.position > 0 && player.mediaStatus !== MediaPlayer.EndOfMedia)
            onKeepChanged: block.keepLive = keep
            Component.onDestruction: block.keepLive = false
            readonly property real aspect: player.metaData.value(MediaMetaData.Resolution)
                                           ? player.metaData.value(MediaMetaData.Resolution).width
                                             / Math.max(1, player.metaData.value(MediaMetaData.Resolution).height)
                                           : 0
            readonly property alias player: player

            MediaPlayer {
                id: player
                source: block.source
                videoOutput: output
                audioOutput: AudioOutput { }
                onMediaStatusChanged: {
                    if (mediaStatus !== MediaPlayer.LoadedMedia || playbackState !== MediaPlayer.StoppedState)
                        return
                    if (block.wantPlay) {
                        block.wantPlay = false
                        play()
                    } else {
                        pause() // shows the first frame, over the poster
                    }
                }
            }
            VideoOutput {
                id: output
                anchors.fill: parent
                fillMode: VideoOutput.PreserveAspectFit
            }
        }
    }

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
        visible: block.live !== null && block.live.player.error !== MediaPlayer.NoError
        width: parent.width - 20
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: block.live !== null ? block.live.player.errorString : ""
        color: "#ffffff"
    }

    // Play / pause
    Rectangle {
        anchors.centerIn: parent
        width: 48
        height: 48
        radius: 24
        // Dark and ringed: readable on any frame, in any theme
        color: "#b3000000"
        border.color: "#59ffffff"
        border.width: 1
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
            onClicked: {
                if (block.live === null)
                    block.wantPlay = true
                else if (block.playing)
                    block.live.player.pause()
                else
                    block.live.player.play()
            }
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Styles.Primitives.spacing.sp075
        width: 28
        height: 28
        radius: Theme.radius
        color: "#b3000000"
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
                if (block.live !== null)
                    block.live.player.pause()
                block.expandRequested()
            }
        }
    }

    // Progress; click to seek
    Rectangle {
        readonly property var player: block.live !== null ? block.live.player : null
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 5
        color: "#40ffffff"
        visible: player !== null && player.duration > 0 && (block.playing || hover.hovered || player.position > 0)

        Rectangle {
            width: parent.player ? parent.width * parent.player.position / Math.max(1, parent.player.duration) : 0
            height: parent.height
            color: Theme.accent
        }
        MouseArea {
            anchors.fill: parent
            anchors.topMargin: -6
            cursorShape: Qt.PointingHandCursor
            onClicked: (mouse) => parent.player.position = parent.player.duration * mouse.x / width
        }
    }
}
