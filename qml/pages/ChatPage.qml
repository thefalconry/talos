// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import Talos.Native

// One conversation: header, messages, the live turn and the composer.
Rectangle {
    id: page

    readonly property var win: C.ApplicationWindow.window
    readonly property var chat: App.session.chat
    readonly property bool hasChat: App.session.selectedChatId !== ""
    readonly property int column: Math.max(240, Math.min(Theme.column, width - 32))
    readonly property bool isEmpty: hasChat && list.count === 0 && !App.session.historyLoading
                                    && !App.session.turn.active

    color: Theme.bg

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Header ──────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 4

            IconButton {
                visible: page.win.narrow
                iconName: "arrow-left"
                tip: qsTr("Chats")
                onClicked: App.session.clearSelection()
            }
            IconButton {
                visible: !page.win.narrow && !page.win.sidebarOpen
                iconName: "sidebar"
                tip: App.keys(qsTr("Show sidebar  (Ctrl+B)"))
                onClicked: page.win.toggleSidebar()
            }
            IconButton {
                visible: !page.win.narrow && !page.win.sidebarOpen
                iconName: "plus"
                tip: App.keys(qsTr("New chat  (Ctrl+N)"))
                enabled: page.win.connected
                onClicked: page.win.newChat()
            }
            Text {
                text: page.hasChat ? (page.chat.title || "") : ""
                color: Theme.text
                font.pixelSize: Theme.body + 1
                font.weight: Font.Medium
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.leftMargin: 8

                TapHandler {
                    enabled: page.hasChat
                    onDoubleTapped: page.win.renameChat(App.session.selectedChatId, page.chat.title)
                }
            }

            // How full the model's context window is.
            ContextRing {
                visible: page.hasChat && page.chat.contextKnown === true
                pct: page.chat.contextPct || 0
                warn: page.chat.contextWarn === true
                label: page.chat.contextLabel || ""
                onResetRequested: page.win.confirm(
                    qsTr("Start a fresh session?"),
                    qsTr("Talon forgets this conversation's working context and clears its transcript. Long-term memory is kept."),
                    qsTr("Reset"), () => App.session.resetChat(App.session.selectedChatId))
            }
            IconButton {
                visible: page.hasChat && App.voice.canSpeak
                iconName: "mic"
                tip: qsTr("Voice mode")
                onClicked: page.win.startVoice()
            }
            IconButton {
                visible: page.hasChat
                iconName: "more"
                iconWeight: 2.6
                tip: qsTr("Chat options")
                onClicked: chatMenu.popup(this, 0, height + 4)

                TMenu {
                    id: chatMenu
                    TMenuItem {
                        text: qsTr("Rename")
                        iconName: "edit"
                        onTriggered: page.win.renameChat(App.session.selectedChatId, page.chat.title)
                    }
                    TMenuItem {
                        text: qsTr("Search this chat")
                        iconName: "search"
                        hint: "Ctrl+F"
                        onTriggered: page.win.searchChat()
                    }
                    TMenuItem {
                        text: qsTr("Model and effort")
                        iconName: "cpu"
                        onTriggered: modelPicker.open()
                    }
                    TMenuItem {
                        text: qsTr("Save as a file…")
                        iconName: "download"
                        onTriggered: page.win.saveChat(App.session.selectedChatId)
                    }
                    TMenuItem {
                        text: qsTr("Copy as Markdown")
                        iconName: "copy"
                        hint: "Ctrl+Shift+E"
                        onTriggered: page.win.exportChat(App.session.selectedChatId)
                    }
                    TMenuItem {
                        text: qsTr("Check-ins (pulse)")
                        iconName: "pulse"
                        checkable: true
                        checked: page.chat.pulse === true
                        onTriggered: App.session.setPulse(App.session.selectedChatId, checked)
                    }
                    C.MenuSeparator {
                        contentItem: Rectangle { implicitHeight: 1; color: Theme.border }
                    }
                    TMenuItem {
                        text: qsTr("Reset session")
                        iconName: "refresh"
                        onTriggered: page.win.confirm(
                            qsTr("Start a fresh session?"),
                            qsTr("Talon forgets this conversation's working context and clears its transcript. Long-term memory is kept."),
                            qsTr("Reset"), () => App.session.resetChat(App.session.selectedChatId))
                    }
                    TMenuItem {
                        text: qsTr("Delete chat")
                        iconName: "trash"
                        danger: true
                        onTriggered: page.win.confirm(
                            qsTr("Delete this chat?"),
                            qsTr("“%1” and its history are removed from Talon. This can't be undone.").arg(page.chat.title),
                            qsTr("Delete"), () => App.session.deleteChat(App.session.selectedChatId))
                    }
                }
            }
        }

        // ── Connection trouble ──────────────────────────────────────────────
        Rectangle {
            visible: App.session.conn === Session.Error
                     || (App.session.conn === Session.Connecting && App.session.connNote !== "")
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            implicitHeight: bannerRow.implicitHeight + 18
            radius: Theme.radius
            color: App.session.connFatal ? Qt.alpha(Theme.danger, 0.12) : Theme.sunken
            border.width: 1
            border.color: App.session.connFatal ? Qt.alpha(Theme.danger, 0.4) : Theme.border

            RowLayout {
                id: bannerRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 10

                Icon {
                    name: App.session.connFatal ? "alert" : "offline"
                    color: App.session.connFatal ? Theme.danger : Theme.textSecondary
                    size: 17
                }
                Text {
                    text: App.session.connNote || App.session.connError
                    color: Theme.text
                    font.pixelSize: Theme.small + 1
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                TButton {
                    visible: App.session.conn === Session.Error
                    compact: true
                    text: App.session.connFatal ? qsTr("Fix") : qsTr("Retry now")
                    onClicked: App.session.connFatal ? page.win.openSettings("connection")
                                                     : App.session.start()
                }
            }
        }

        // ── Messages ────────────────────────────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list

                // Follow the newest message unless the reader scrolled away.
                property bool stick: true
                property bool adjusting: false
                readonly property real gap: originY + contentHeight - contentY - height

                function toEnd() {
                    adjusting = true
                    positionViewAtEnd()
                    adjusting = false
                }
                function settle() {
                    if (stick)
                        Qt.callLater(toEnd)
                }

                anchors.fill: parent
                visible: page.hasChat
                model: App.session.messages
                clip: true
                spacing: 18
                topMargin: 12
                cacheBuffer: 2400
                reuseItems: false
                boundsBehavior: Flickable.StopAtBounds

                onContentYChanged: {
                    if (!adjusting)
                        stick = gap < 48
                    // Near the top: page in what came before.
                    if (!adjusting && contentY - originY < 240 && contentHeight > height
                            && App.session.hasMoreHistory && !App.session.loadingOlder)
                        App.session.loadOlder()
                }
                onContentHeightChanged: settle()
                onHeightChanged: settle()
                onCountChanged: settle()

                Connections {
                    target: App.session
                    function onSelectionChanged() {
                        list.stick = true
                        Qt.callLater(list.toEnd)
                    }
                }
                Connections {
                    target: App.session.messages
                    function onModelReset() {
                        list.stick = true
                        Qt.callLater(list.toEnd)
                    }
                }

                header: Item {
                    width: list.width
                    height: App.session.loadingOlder ? 44 : 0

                    Spinner {
                        anchors.centerIn: parent
                        visible: App.session.loadingOlder
                    }
                }

                delegate: MessageRow {
                    width: list.width
                    column: page.column
                }

                footer: LiveTurn {
                    width: list.width
                    column: page.column
                }

                C.ScrollBar.vertical: C.ScrollBar { }
            }

            Spinner {
                anchors.centerIn: parent
                size: 22
                visible: page.hasChat && App.session.historyLoading
            }

            // A chat with nothing in it yet.
            Column {
                visible: page.isEmpty
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -30
                width: page.column
                spacing: 14

                BrandMark {
                    size: 44
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: {
                        const h = new Date().getHours()
                        return h < 5 ? qsTr("Up late?") : h < 12 ? qsTr("Good morning")
                             : h < 18 ? qsTr("Good afternoon") : qsTr("Good evening")
                    }
                    color: Theme.text
                    font.family: Theme.serif
                    font.pixelSize: Theme.fs(30)
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("Ask %1 anything, or type / for commands.").arg(App.session.botName)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.body
                }
            }

            // Nothing open (two-pane layout with no chats at all).
            Column {
                visible: !page.hasChat
                anchors.centerIn: parent
                width: Math.min(page.column, 420)
                spacing: 14

                BrandMark {
                    size: 44
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: page.win.connected ? qsTr("Start a conversation")
                        : App.session.conn === Session.Connecting ? qsTr("Connecting to Talon…")
                        : qsTr("Not connected to Talon")
                    color: Theme.text
                    font.family: Theme.serif
                    font.pixelSize: Theme.fs(26)
                }
                TButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    kind: "primary"
                    iconName: page.win.connected ? "plus" : "settings"
                    text: page.win.connected ? qsTr("New chat") : qsTr("Connection settings")
                    onClicked: page.win.connected ? page.win.newChat()
                                                  : page.win.openSettings("connection")
                }
            }

            // Back to the newest message.
            IconButton {
                visible: page.hasChat && !list.stick && list.gap > 240
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                iconName: "arrow-down"
                tip: qsTr("Jump to latest")
                size: 34
                background: Rectangle {
                    radius: 17
                    color: Theme.raised
                    border.width: 1
                    border.color: Theme.border
                }
                onClicked: {
                    list.stick = true
                    list.toEnd()
                }
            }
        }

        Composer {
            id: composer
            visible: page.hasChat
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: page.column
            Layout.bottomMargin: 14
            Layout.topMargin: 4
            onModelRequested: modelPicker.open()
        }
    }

    function openModelPicker() { modelPicker.open() }
    function setDraft(text) { composer.setText(text) }
    function acceptShare(text, files) {
        if (text !== "")
            composer.setText(text)
        for (const f of files)
            composer.attach(f)
    }

    ModelPicker { id: modelPicker }

    // Drop files anywhere on the conversation to attach them.
    DropArea {
        id: drop
        anchors.fill: parent
        enabled: page.hasChat
        keys: ["text/uri-list"]
        onDropped: event => {
            for (const url of event.urls)
                composer.attach(url)
            event.accept()
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 10
            visible: drop.containsDrag
            radius: Theme.radiusLarge
            color: Theme.accentSoft
            border.width: 2
            border.color: Theme.accent

            Column {
                anchors.centerIn: parent
                spacing: 8

                Icon {
                    name: "upload"
                    size: 30
                    color: Theme.accent
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    text: qsTr("Drop to attach")
                    color: Theme.text
                    font.pixelSize: Theme.title
                    font.weight: Font.Medium
                }
            }
        }
    }
}
