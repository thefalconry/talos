// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import Talos.Native

// Renders a message's blocks: prose as Markdown, fenced code as CodeBlock.
// The repeater is keyed by count, so while text streams in only the block
// that changed is updated; nothing is torn down and rebuilt per token.
Column {
    id: root

    property var blocks: []
    property color color: Theme.text
    signal linkActivated(string link)

    spacing: 10

    Repeater {
        model: root.blocks.length

        Loader {
            id: slot
            required property int index
            readonly property var block: root.blocks[index] || ({})
            readonly property bool isCode: block.type === "code"

            readonly property bool isMath: block.type === "math"
            readonly property bool isChart: isCode && block.lang === "mermaid"
                                            && block.closed !== false

            width: root.width
            sourceComponent: isMath ? mathBlock : isChart ? chartBlock : isCode ? codeBlock : prose

            Component {
                id: prose
                TextEdit {
                    width: slot.width
                    text: slot.block.text || ""
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
                        text: slot.block.text || ""
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
                            source: slot.block.code || ""
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
                        code: slot.block.code || ""
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
                    code: slot.block.code || ""
                    language: slot.block.lang || ""
                    closed: slot.block.closed !== false
                }
            }
        }
    }
}
