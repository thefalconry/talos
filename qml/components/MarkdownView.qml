// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import Talos.Native

// Renders a message's blocks: prose as Markdown, fenced code as CodeBlock.
// There is one slot per block, and the slots are added and taken away one
// at a time as the count changes: while text streams in, a new paragraph
// costs one new item and a new word costs the last paragraph's layout,
// nothing more. (A repeater over a bare number rebuilds every item when
// the number changes.)
Column {
    id: root

    property var blocks: []
    property color color: Theme.text
    signal linkActivated(string link)

    spacing: 10

    ListModel { id: slots }
    function fitSlots() {
        const wanted = blocks.length
        while (slots.count > wanted)
            slots.remove(slots.count - 1)
        while (slots.count < wanted)
            slots.append({ slot: slots.count })
    }
    onBlocksChanged: fitSlots()
    Component.onCompleted: fitSlots()

    Repeater {
        model: slots

        Loader {
            id: slot
            required property int index
            readonly property var block: root.blocks[index] || ({})
            readonly property bool isCode: block.type === "code"
            // As strings of their own: `block` is a new object whenever
            // anything streams in, but a string property only announces a
            // change when its text differs. A text item handed its text
            // again parses and lays it all out again, changed or not.
            readonly property string blockText: block.text || ""
            readonly property string blockCode: block.code || ""

            readonly property bool isMath: block.type === "math"
            readonly property bool isChart: isCode && block.lang === "mermaid"
                                            && block.closed !== false

            width: root.width
            sourceComponent: isMath ? mathBlock : isChart ? chartBlock : isCode ? codeBlock : prose

            Component {
                id: prose
                TextEdit {
                    width: slot.width
                    text: slot.blockText
                    readOnly: true
                    // On a phone a drag scrolls the conversation; selection
                    // there is offered through "Copy" on the message.
                    selectByMouse: !App.mobile
                    textFormat: TextEdit.MarkdownText
                    wrapMode: TextEdit.Wrap
                    color: root.color
                    font.pixelSize: Theme.reading
                    selectionColor: Theme.accentSoft
                    selectedTextColor: Theme.text
                    onLinkActivated: link => root.linkActivated(link)

                    HoverHandler {
                        cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor
                                                               : Qt.IBeamCursor
                    }
                }
            }
            Component {
                id: mathBlock
                // A displayed formula, as close as plain characters get.
                Rectangle {
                    width: slot.width
                    implicitHeight: formula.implicitHeight + 22
                    radius: Theme.radius
                    color: Theme.sunken

                    TextEdit {
                        id: formula
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, parent.width - 28)
                        text: slot.blockText
                        readOnly: true
                        selectByMouse: !App.mobile
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.Wrap
                        horizontalAlignment: TextEdit.AlignHCenter
                        color: root.color
                        font.pixelSize: Theme.reading + 2
                        font.italic: true
                        font.family: "serif"
                        selectionColor: Theme.accentSoft
                        selectedTextColor: Theme.text
                    }
                }
            }
            Component {
                id: chartBlock
                // A flowchart is drawn; any other diagram, or one that
                // can't be read, is shown as its source.
                Column {
                    id: chartHolder
                    property bool showSource: false
                    width: slot.width
                    spacing: 6

                    Rectangle {
                        visible: chart.valid && !chartHolder.showSource
                        width: parent.width
                        height: visible ? chart.height + 20 : 0
                        radius: Theme.radius
                        color: Theme.sunken
                        border.width: 1
                        border.color: Theme.border
                        clip: true

                        FlowChart {
                            id: chart
                            x: Math.max(10, (parent.width - width) / 2)
                            y: 10
                            width: Math.min(implicitWidth, parent.width - 20)
                            height: implicitWidth > 0 ? implicitHeight * (width / implicitWidth) : 0
                            source: slot.blockCode
                            ink: Theme.text
                            line: Theme.textTertiary
                            fill: Theme.surface
                            paper: Theme.sunken
                            font.pixelSize: Theme.small + 1
                        }
                    }
                    CodeBlock {
                        visible: !chart.valid || chartHolder.showSource
                        width: parent.width
                        code: slot.blockCode
                        language: "mermaid"
                    }
                    Text {
                        visible: chart.valid
                        text: chartHolder.showSource ? qsTr("Show the diagram") : qsTr("Show the source")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.small
                        TapHandler { onTapped: chartHolder.showSource = !chartHolder.showSource }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }
            }
            Component {
                id: codeBlock
                CodeBlock {
                    width: slot.width
                    code: slot.blockCode
                    language: slot.block.lang || ""
                    closed: slot.block.closed !== false
                }
            }
        }
    }
}
