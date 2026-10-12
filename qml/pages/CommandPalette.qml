// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import Talos.Native

// Ctrl+K: run an action, jump to a chat, or search every message.
// Ctrl+F: search the messages of the open chat only.
C.Popup {
    id: cmd

    readonly property var win: C.ApplicationWindow.window
    property var hits: []
    property int current: 0
    property bool searching: false
    /// Set while the search is confined to one chat; then only its messages
    /// are listed.
    property string scopeChatId
    property string scopeTitle

    function openInChat(chatId, title) {
        scopeChatId = chatId
        scopeTitle = title
        open()
    }

    readonly property var actions: [
        { label: qsTr("New chat"), icon: "plus", hint: "Ctrl+N", keys: "new create start", run: () => win.newChat() },
        { label: qsTr("Settings"), icon: "settings", hint: "Ctrl+,", keys: "preferences options", run: () => win.openSettings() },
        { label: qsTr("Connection"), icon: "server", hint: "", keys: "server profile token certificate mtls", run: () => win.openSettings("connection") },
        { label: qsTr("Toggle sidebar"), icon: "sidebar", hint: "Ctrl+B", keys: "hide show panel", run: () => win.toggleSidebar() },
        { label: qsTr("Model and effort for this chat"), icon: "cpu", hint: "", keys: "model reasoning backend", run: () => win.openModelPicker() },
        { label: qsTr("Copy conversation as Markdown"), icon: "copy", hint: "Ctrl+Shift+E", keys: "export share", run: () => win.exportChat(App.session.selectedChatId) },
        { label: qsTr("Stop the current response"), icon: "stop", hint: "Ctrl+.", keys: "interrupt cancel", run: () => App.session.interrupt() },
        { label: qsTr("Voice mode"), icon: "mic", hint: "", keys: "speak talk listen", run: () => win.startVoice() },
        { label: qsTr("Plugins and skills"), icon: "plug", hint: "", keys: "extensions mcp", run: () => win.openSettings("extensions") },
        { label: qsTr("What Talon remembers"), icon: "database", hint: "", keys: "memory facts", run: () => win.openSettings("memory") },
        { label: qsTr("Devices and mesh"), icon: "phone", hint: "", keys: "mesh device location", run: () => win.openSettings("mesh") },
        { label: qsTr("Daemon logs"), icon: "list", hint: "", keys: "log errors diagnostics", run: () => win.openSettings("logs") },
        { label: qsTr("Consolidate memory now (dream)"), icon: "spark", hint: "", keys: "dream diary sleep",
          run: () => App.session.control("dream", r => win.toast(r.message)) },
        { label: qsTr("Light theme"), icon: "sun", hint: "", keys: "appearance", run: () => App.prefs.theme = "light" },
        { label: qsTr("Dark theme"), icon: "sun", hint: "", keys: "appearance night", run: () => App.prefs.theme = "dark" },
        { label: qsTr("Follow the system theme"), icon: "sun", hint: "", keys: "appearance auto", run: () => App.prefs.theme = "system" },
        { label: qsTr("Keyboard shortcuts"), icon: "keyboard", hint: "Ctrl+/", keys: "help keys", run: () => win.openShortcuts() },
        { label: qsTr("Lock the app"), icon: "lock", hint: "Ctrl+L", keys: "passcode", run: () => App.lock.lock() }
    ]

    readonly property var rows: {
        const q = input.text.trim().toLowerCase()
        const out = []
        if (scopeChatId !== "") {
            for (const h of hits) {
                out.push({ kind: "message", label: h.text, icon: "search", hint: App.relativeTime(h.ts),
                           detail: h.fromUser ? qsTr("You") : App.session.botName,
                           run: () => win.openChat(h.chatId) })
            }
            return out
        }
        for (const a of actions) {
            if (q === "" || a.label.toLowerCase().includes(q) || a.keys.includes(q))
                out.push({ kind: "action", label: a.label, icon: a.icon, hint: App.keys(a.hint), detail: "", run: a.run })
        }
        const chats = App.session.chatSummaries()
        let listed = 0
        for (const c of chats) {
            if (q !== "" && !c.title.toLowerCase().includes(q))
                continue
            if (q === "" && listed >= 6)
                break
            ++listed
            const id = c.id
            out.push({ kind: "chat", label: c.title, icon: "chat", hint: "", detail: c.preview,
                       run: () => win.openChat(id) })
        }
        for (const h of hits) {
            const id = h.chatId
            out.push({ kind: "message", label: h.text, icon: "search", hint: App.relativeTime(h.ts),
                       detail: h.chatTitle, run: () => win.openChat(id) })
        }
        return out
    }
    onRowsChanged: current = Math.min(current, Math.max(0, rows.length - 1))

    function run(index) {
        const row = rows[index]
        if (!row)
            return
        close()
        row.run()
    }

    parent: C.Overlay.overlay
    modal: true
    focus: true
    padding: 0
    width: Math.min(parent.width - 24, 600)
    x: Math.round((parent.width - width) / 2)
    y: Math.max(12, Math.round(parent.height * 0.14))
    closePolicy: C.Popup.CloseOnEscape | C.Popup.CloseOnPressOutside

    onAboutToShow: {
        input.text = ""
        hits = []
        current = 0
    }
    onClosed: {
        scopeChatId = ""
        scopeTitle = ""
    }
    onOpened: input.forceActiveFocus()

    C.Overlay.modal: Rectangle { color: Theme.scrim }
    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }

    // Full-text search runs on the daemon, a beat after typing stops.
    Timer {
        id: debounce
        interval: 260
        onTriggered: {
            const q = input.text.trim()
            if (q.length < 2) {
                cmd.hits = []
                cmd.searching = false
                return
            }
            cmd.searching = true
            const scope = cmd.scopeChatId
            App.session.search(q, scope, function (results) {
                if (input.text.trim() !== q || cmd.scopeChatId !== scope)
                    return
                cmd.hits = results.slice(0, 20)
                cmd.searching = false
            })
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 6
            Layout.leftMargin: 16
            spacing: 10

            Icon { name: "search"; size: 18; color: Theme.textTertiary }
            C.TextField {
                id: input
                Layout.fillWidth: true
                implicitHeight: 44
                placeholderText: cmd.scopeChatId !== ""
                                 ? qsTr("Search in “%1”").arg(cmd.scopeTitle)
                                 : qsTr("Search chats and messages, or run a command")
                placeholderTextColor: Theme.textTertiary
                color: Theme.text
                font.pixelSize: Theme.reading
                background: null
                selectByMouse: true
                onTextChanged: {
                    cmd.current = 0
                    debounce.restart()
                }
                Keys.onDownPressed: cmd.current = Math.min(cmd.rows.length - 1, cmd.current + 1)
                Keys.onUpPressed: cmd.current = Math.max(0, cmd.current - 1)
                onAccepted: cmd.run(cmd.current)
            }
            Spinner { visible: cmd.searching; size: 15 }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        ListView {
            id: results
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight + 12, 400)
            topMargin: 6
            bottomMargin: 6
            clip: true
            model: cmd.rows.length
            currentIndex: cmd.current
            boundsBehavior: Flickable.StopAtBounds
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

            delegate: Item {
                id: hit
                required property int index
                readonly property var data_: cmd.rows[index] || ({})

                Accessible.role: Accessible.ListItem
                Accessible.name: (data_.label || "") + (data_.detail ? ", " + data_.detail : "")
                Accessible.selected: index === cmd.current
                Accessible.onPressAction: cmd.run(index)
                width: results.width
                height: data_.detail ? 50 : 38

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    radius: 9
                    color: hit.index === cmd.current ? Theme.hover : "transparent"
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 12

                    Icon { name: hit.data_.icon || "chat"; size: 16; color: Theme.textSecondary }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        Text {
                            text: hit.data_.label || ""
                            color: Theme.text
                            font.pixelSize: Theme.body
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: (hit.data_.detail || "") !== ""
                            text: hit.data_.detail || ""
                            color: Theme.textTertiary
                            font.pixelSize: Theme.small
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Text {
                        text: hit.data_.hint || ""
                        color: Theme.textTertiary
                        font.pixelSize: Theme.small
                    }
                }
                HoverHandler {
                    onHoveredChanged: if (hovered) cmd.current = hit.index
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler { onTapped: cmd.run(hit.index) }
            }

            Text {
                anchors.centerIn: parent
                visible: cmd.rows.length === 0
                text: qsTr("Nothing matches")
                color: Theme.textTertiary
                font.pixelSize: Theme.body
            }
        }
    }
}
