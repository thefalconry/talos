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

            width: root.width
            sourceComponent: isCode ? codeBlock : prose

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
