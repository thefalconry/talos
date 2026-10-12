// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import Talos.Native

// A small window of its own for one question: summoned by a system-wide
// shortcut or `talos --quick`, without bringing the whole app forward. The
// question starts a new chat, and the answer streams in right here.
Window {
    id: quick

    /// The main window: links and "open in Talos" go through it.
    property var main
    property string question
    property var answer: []
    readonly property var turn: App.session.turn
    readonly property bool waiting: question !== "" && answer.length === 0
                                    && (turn.active || turn.text === "")

    function present() {
        if (App.lock.locked) {
            App.showWindow()
            return
        }
        visible = true
        raise()
        requestActivate()
        input.forceActiveFocus()
        input.selectAll()
    }

    function ask(typed) {
        const text = (typed === undefined ? input.text : typed).trim()
        if (text === "")
            return
        question = text
        answer = []
        input.clear()
        App.session.askInNewChat(text)
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    // Its scroll bar, like the main window's, is drawn from the palette.
    palette {
        text: Theme.text
        mid: Theme.borderStrong
        dark: Theme.textTertiary
        highlight: Theme.accent
        highlightedText: Theme.accentInk
        link: Theme.accent
        linkVisited: Theme.accent
    }
    width: Math.min(620, Screen.width - 40)
    height: card.implicitHeight + 24
    x: Screen.virtualX + Math.round((Screen.width - width) / 2)
    y: Screen.virtualY + Math.round(Screen.height * 0.18)
    title: qsTr("Ask Talon")

    onActiveChanged: {
        // Clicking away puts it aside, unless an answer is still coming.
        if (!active && !turn.active && question === "")
            visible = false
    }

    Connections {
        target: App.session
        function onTurnEnded(chatId) {
            if (quick.question === "" || chatId !== App.session.selectedChatId)
                return
            const last = App.session.lastMessage()
            if (last.role === "assistant")
                quick.answer = last.blocks
        }
    }

    Shortcut { sequences: ["Escape"]; onActivated: quick.visible = false }

    Rectangle {
        id: card
        x: 12
        y: 12
        width: parent.width - 24
        implicitHeight: column.implicitHeight + 20
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        ColumnLayout {
            id: column
            x: 14
            y: 10
            width: parent.width - 28
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Icon { name: "spark"; size: 18; color: Theme.accent }
                C.TextField {
                    id: input
                    Layout.fillWidth: true
                    implicitHeight: 40
                    placeholderText: App.session.conn === Session.Connected
                                     ? qsTr("Ask %1 anything").arg(App.session.botName)
                                     : qsTr("Not connected to Talon")
                    placeholderTextColor: Theme.textTertiary
                    enabled: App.session.conn === Session.Connected
                    color: Theme.text
                    font.pixelSize: Theme.reading
                    background: null
                    selectByMouse: true
                    onAccepted: quick.ask(undefined)
                    Accessible.name: qsTr("Question for Talon")
                }
                IconButton {
                    iconName: "external"
                    tip: qsTr("Open in Talos")
                    onClicked: {
                        quick.visible = false
                        App.showWindow()
                    }
                }
                IconButton {
                    iconName: "x"
                    tip: qsTr("Close  (Esc)")
                    onClicked: quick.visible = false
                }
            }

            Rectangle {
                visible: quick.question !== ""
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.border
            }
            Text {
                visible: quick.question !== ""
                Layout.fillWidth: true
                text: quick.question
                color: Theme.textSecondary
                font.pixelSize: Theme.small + 1
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
            RowLayout {
                visible: quick.waiting
                spacing: 8
                Spinner { size: 15 }
                Text {
                    text: qsTr("Thinking…")
                    color: Theme.textTertiary
                    font.pixelSize: Theme.small + 1
                }
            }
            Flickable {
                id: scroller
                visible: quick.question !== "" && !quick.waiting
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(reply.implicitHeight, 380)
                Layout.bottomMargin: 4
                contentHeight: reply.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                // Follow the answer down as it is written.
                onContentHeightChanged: if (quick.turn.active) contentY = Math.max(0, contentHeight - height)

                MarkdownView {
                    id: reply
                    width: scroller.width
                    blocks: quick.answer.length > 0 ? quick.answer : quick.turn.blocks
                    onLinkActivated: link => quick.main.openLink(link)
                }
                C.ScrollBar.vertical: C.ScrollBar { }
            }
        }
    }
}
