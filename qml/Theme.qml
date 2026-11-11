pragma Singleton
import QtQuick
import QtCore
import IgittStyle as Styles

// The canvas' own colours, made from the roles of the igitt theme in use
// (Styles.TokenInterface), plus preferences. Everything else uses the
// style's controls and tokens directly.
QtObject {
    id: theme

    readonly property var s: Styles.TokenInterface.semantics
    readonly property bool dark: Styles.TokenInterface.dark

    // Preferences, kept between sessions
    readonly property Settings prefs: Settings {
        category: "preferences"
        property bool wheelZooms: true
        property string uiFamily: "Inter"
        property real uiSize: 10
        property string codeFamily: "JetBrains Mono"
        property real codeSize: 10
    }
    readonly property font uiFont: Qt.font({ family: prefs.uiFamily, pointSize: prefs.uiSize })
    readonly property font monoFont: Qt.font({ family: prefs.codeFamily, pointSize: prefs.codeSize })

    // The canvas sits a step below the window; cards are elevated on it
    readonly property color canvas: Qt.tint(s.background_default, Styles.Primitives.alpha(s.background_muted, 0.6))
    readonly property color gridDot: Styles.Primitives.alpha(s.stroke_field, 0.7)
    readonly property color card: s.background_elevated
    readonly property color cardBorder: s.stroke_subtle
    readonly property color accent: s.primary_default
    readonly property color text: s.text_default
    readonly property color textMuted: s.text_muted
    readonly property color link: s.text_link
    readonly property color searchHighlight: Styles.Primitives.alpha(s.warning_default, dark ? 0.45 : 0.35)
    readonly property int radius: Styles.Primitives.radius.r4

    // Note colours are the theme's status colours over the card surface
    readonly property var noteKeys: ["", "yellow", "green", "blue", "pink", "purple"]

    function swatch(key) {
        switch (key) {
        case "yellow": return s.warning_default
        case "green":  return s.success_default
        case "blue":   return s.info_default
        case "pink":   return s.danger_default
        case "purple": return s.special_default
        default:       return s.background_elevated
        }
    }
    function noteColor(key) {
        return key === "" ? card : Qt.tint(card, Styles.Primitives.alpha(swatch(key), dark ? 0.16 : 0.20))
    }
}
