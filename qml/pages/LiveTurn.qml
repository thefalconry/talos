// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import Talos.Native

// The turn in progress, shown under the last message: the model's
// reasoning, the tools it is running and the reply as it streams in.
Item {
    id: live

    property int column: Theme.column
    readonly property var turn: App.session.turn
    readonly property var win: C.ApplicationWindow.window
    readonly property bool showing: turn.active || turn.text !== "" || turn.reasoning !== ""
                                    || turn.tools.length > 0
    readonly property bool waiting: turn.active && turn.text === ""
    property double now: Date.now()

    implicitHeight: (showing ? body.implicitHeight + 18 : 0) + 22

    Timer {
        interval: 1000
        repeat: true
        running: live.turn.active
        triggeredOnStart: true
        onTriggered: live.now = Date.now()
    }

    Column {
        id: body
        visible: live.showing
        y: 18
        width: live.column
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 10

        // Reasoning, folded away once there is an answer to read.
        Column {
            id: thinking
            visible: live.turn.reasoning !== ""
            width: parent.width
            spacing: 6
            property bool open: App.prefs.showReasoning

            Row {
                spacing: 7

                Icon {
                    name: "spark"
                    size: 15
                    color: Theme.textTertiary
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: live.turn.text === "" ? qsTr("Thinking…") : qsTr("Thought process")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.small + 1
                    anchors.verticalCenter: parent.verticalCenter
                }
                Icon {
                    name: thinking.open ? "chevron-up" : "chevron-down"
                    size: 13
                    color: Theme.textTertiary
                    anchors.verticalCenter: parent.verticalCenter
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: thinking.open = !thinking.open }
            }
            Item {
                visible: thinking.open
                width: parent.width
                height: reasoningText.implicitHeight

                Rectangle {
                    x: 6
                    width: 2
                    height: parent.height
                    radius: 1
                    color: Theme.border
                }
                Text {
                    id: reasoningText
                    x: 20
                    width: parent.width - 20
                    text: live.turn.reasoning
                    color: Theme.textSecondary
                    font.pixelSize: Theme.small + 1
                    wrapMode: Text.Wrap
                    lineHeight: 1.3
                    textFormat: Text.PlainText
                }
            }
        }

        ToolList {
            visible: live.turn.tools.length > 0
            width: parent.width
            tools: live.turn.tools
        }

        MarkdownView {
            visible: live.turn.text !== ""
            width: parent.width
            blocks: live.turn.blocks
            onLinkActivated: link => live.win.openLink(link)
        }

        // Nothing to show yet: say that work is happening, and for how long.
        Row {
            visible: live.waiting && live.turn.reasoning === ""
            spacing: 9
            height: 22

            Row {
                spacing: 4
                anchors.verticalCenter: parent.verticalCenter

                Repeater {
                    model: 3

                    Rectangle {
                        required property int index
                        width: 6
                        height: 6
                        radius: 3
                        color: Theme.accent

                        SequentialAnimation on opacity {
                            running: live.waiting
                            loops: Animation.Infinite
                            PauseAnimation { duration: index * 160 }
                            NumberAnimation { from: 0.25; to: 1; duration: 320 }
                            NumberAnimation { from: 1; to: 0.25; duration: 320 }
                            PauseAnimation { duration: (2 - index) * 160 }
                        }
                    }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.textTertiary
                font.pixelSize: Theme.small + 1
                text: {
                    const s = live.turn.startedMs > 0
                            ? Math.max(0, Math.floor((live.now - live.turn.startedMs) / 1000)) : 0
                    const label = live.turn.continuing ? qsTr("Still working")
                                : live.turn.tools.length > 0 ? qsTr("Working") : qsTr("Thinking")
                    return s >= 3 ? label + " · " + s + " s" : label
                }
            }
        }
    }
}
