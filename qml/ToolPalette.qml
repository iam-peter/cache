import QtQuick
import QtQuick.Controls
import IgittStyle as Styles
import Cache

// The tools along the bottom of the canvas, as in FigJam. With the pen or
// highlighter on, its colours show above it.
Rectangle {
    id: toolbox

    required property Item canvas
    signal templatesRequested()

    readonly property var shapes: [["rect", "square", qsTr("Rectangle (R)")], ["ellipse", "circle", qsTr("Ellipse (O)")],
                                   ["diamond", "diamond", qsTr("Diamond (D)")], ["triangle", "triangle", qsTr("Triangle")]]
    readonly property var shapeTool: shapes.find(s => s[0] === canvas.tool) || shapes[0]
    property string lastShape: "rect"

    width: tools.implicitWidth + 2 * Styles.Primitives.spacing.sp075
    height: tools.implicitHeight + 2 * Styles.Primitives.spacing.sp075
    radius: Theme.radius * 2
    color: Theme.s.background_elevated
    border.color: Theme.s.stroke_subtle

    Styles.Shadow {
        z: -1
        anchors.fill: parent
        radius: parent.radius
        depth: 3
    }

    component Tool: IconButton {
        id: tool
        property string name
        property string tip
        styles: [Styles.ButtonStyle.medium]
        checkable: true
        checked: toolbox.canvas.tool === name
        onClicked: toolbox.canvas.tool = name
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.text: tip
    }

    // As tall as a button: a taller separator made the row taller and left
    // more room below the buttons than above
    Row {
        id: tools
        anchors.centerIn: parent
        spacing: Styles.Primitives.spacing.sp025

        Tool { name: "select"; iconName: "mouse-pointer-2"; tip: qsTr("Select and move (V); drag on empty space to select several") }
        Tool { name: "hand"; iconName: "hand"; tip: qsTr("Pan (H, or hold Space)") }
        ToolSeparator { height: 20; anchors.verticalCenter: parent.verticalCenter }
        Tool { name: "note"; iconName: "sticky-note"; tip: qsTr("Note (N)") }
        Tool { name: "text"; iconName: "type"; tip: qsTr("Text (T)") }
        Tool { name: "code"; iconName: "code"; tip: qsTr("Code snippet (K)") }
        Tool {
            id: shapeButton
            name: toolbox.shapeTool[0] === toolbox.canvas.tool ? toolbox.canvas.tool : toolbox.lastShape
            iconName: (toolbox.shapes.find(s => s[0] === name) || toolbox.shapes[0])[1]
            tip: qsTr("Shape (R, O, D); right-click to choose")
            checked: toolbox.shapes.some(s => s[0] === toolbox.canvas.tool)
            onClicked: toolbox.canvas.tool = name
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: shapeMenu.popup(shapeButton, 0, -shapeMenu.implicitHeight)
            }
            Menu {
                id: shapeMenu
                Repeater {
                    model: toolbox.shapes
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData[2]
                        icon.source: Icons.source(modelData[1])
                        onTriggered: {
                            toolbox.lastShape = modelData[0]
                            toolbox.canvas.tool = modelData[0]
                        }
                    }
                }
            }
        }
        Tool { name: "section"; iconName: "square-dashed"; tip: qsTr("Section (S): drag out an area that groups what is in it") }
        Tool { name: "connector"; iconName: "spline"; tip: qsTr("Connector (L): drag from one item to another, or to empty space for a new note") }
        Tool { name: "pen"; iconName: "pen-line"; tip: qsTr("Pen (P)") }
        Tool { name: "highlighter"; iconName: "highlighter"; tip: qsTr("Highlighter (M)") }
        ToolSeparator { height: 20; anchors.verticalCenter: parent.verticalCenter }
        IconButton {
            styles: [Styles.ButtonStyle.medium]
            iconName: "layout-template"
            onClicked: toolbox.templatesRequested()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Templates")
        }
        IconButton {
            styles: [Styles.ButtonStyle.medium]
            iconName: "map"
            checkable: true
            checked: Theme.prefs.showMinimap
            onClicked: Theme.prefs.showMinimap = checked
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Minimap")
        }
    }

    // Pen colours
    Rectangle {
        visible: toolbox.canvas.tool === "pen" || toolbox.canvas.tool === "highlighter"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.top
        anchors.bottomMargin: Styles.Primitives.spacing.sp075
        width: colours.implicitWidth + 14
        height: colours.implicitHeight + 14
        radius: Theme.radius * 2
        color: Theme.s.background_elevated
        border.color: Theme.s.stroke_subtle
        Row {
            id: colours
            anchors.centerIn: parent
            spacing: 4
            Repeater {
                model: Theme.penColors
                delegate: Rectangle {
                    required property string modelData
                    readonly property bool current: toolbox.canvas.penColor === modelData
                                                    || (toolbox.canvas.tool === "highlighter" && toolbox.canvas.penColor === "" && modelData === "yellow")
                    width: 20
                    height: 20
                    radius: 10
                    color: toolbox.canvas.tool === "highlighter" && modelData === "" ? Theme.swatch("yellow") : Theme.penColor(modelData)
                    border.width: current ? 3 : 1
                    border.color: current ? Theme.accent : Theme.s.stroke_field
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: toolbox.canvas.penColor = parent.modelData
                    }
                }
            }
        }
    }
}
