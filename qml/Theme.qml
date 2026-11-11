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
        property bool showMinimap: true
        property bool snap: true             // smart guides while moving
        property bool showWikiLinks: true    // [[links]] as dotted lines
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
    // Shapes are a little stronger than notes, sections much lighter
    function shapeColor(key) {
        return key === "" ? card : Qt.tint(card, Styles.Primitives.alpha(swatch(key), dark ? 0.28 : 0.32))
    }
    function sectionColor(key) {
        return Styles.Primitives.alpha(key === "" ? s.text_muted : swatch(key), dark ? 0.08 : 0.07)
    }
    function lineColor(key) {
        return key === "" ? s.text_muted : swatch(key)
    }
    readonly property var penColors: ["", "pink", "blue", "green", "yellow", "purple"]
    function penColor(key) {
        return key === "" ? s.text_default : swatch(key)
    }

    // What notes are rendered with (BoardModel::renderSegments)
    readonly property var renderColors: ({
        link: s.text_link,
        text: s.text_default,
        muted: s.text_muted,
        highlight: searchHighlight,
        keyword: s.syntax_keyword,
        type: s.syntax_type,
        string: s.syntax_string,
        number: s.syntax_number,
        comment: s.syntax_comment,
        preprocessor: s.syntax_preprocessor,
        function: s.syntax_function,
    })
    readonly property color codeBackground: s.background_secondary
}
