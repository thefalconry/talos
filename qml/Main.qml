// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Dialogs
import QtQuick.Layouts
import Talos.Native

C.ApplicationWindow {
    id: root

    // Shown from C++ once everything is wired (or kept hidden for the tray).
    visible: false
    width: App.mobile ? 412 : App.prefs.windowWidth
    height: App.mobile ? 892 : App.prefs.windowHeight
    minimumWidth: 340
    minimumHeight: 460
    color: Theme.bg
    // A locked app does not name the open conversation in the task switcher.
    title: App.session.chat.title && !App.lock.locked
           ? App.session.chat.title + " · " + App.session.botName : App.session.botName

    readonly property bool narrow: width < Theme.narrowBelow
    readonly property bool connected: App.session.conn === Session.Connected
    property bool settingsOpen: false
    property string settingsSection: "connection"
    property bool settingsDirect: false
    property bool sidebarOpen: App.prefs.sidebarOpen

    onNarrowChanged: App.session.narrow = narrow
    Component.onCompleted: {
        App.session.narrow = narrow
        App.setLinkColor(Theme.accent)
    }
    Connections {
        target: Theme
        function onAccentChanged() { App.setLinkColor(Theme.accent) }
    }

    onClosing: close => {
        // A phone's back gesture ends up here when nothing on screen took
        // it. It means one step back, and at the chat list it means leave
        // the app running in the background, not quit it.
        if (App.mobile && (goBack() || App.moveToBackground())) {
            close.accepted = false
            return
        }
        if (!App.mobile && visibility === Window.Windowed) {
            App.prefs.windowWidth = width
            App.prefs.windowHeight = height
        }
        if (App.prefs.closeToTray && App.desktop.trayAvailable) {
            close.accepted = false
            root.hide()
            App.session.persist()
        } else {
            App.quit()
        }
    }

    // ── What the rest of the UI calls ───────────────────────────────────────

    function toast(text) { toaster.show(text) }

    function openSettings(section) {
        if (section)
            settingsSection = section
        // Asked for a particular section: on a phone, go straight to it.
        settingsDirect = !!section
        settingsOpen = true
    }

    /// One step back: out of a settings section, out of settings, out of
    /// the open chat. False at the chat list, where there is nowhere left.
    function goBack() {
        if (App.lock.locked)
            return false
        // An open sheet or menu normally takes the back key itself. If one
        // didn't, close it when it can be closed; either way, back never
        // reaches past it to the page underneath.
        for (const popup of [viewer, voiceScreen, commandPalette, shortcuts, renameSheet, confirmSheet]) {
            if (popup.visible) {
                popup.close()
                return true
            }
        }
        if (C.Overlay.overlay.visible)
            return true
        if (settingsOpen) {
            if (!settingsLoader.item.back())
                settingsOpen = false
            return true
        }
        if (narrow && App.session.selectedChatId !== "") {
            App.session.clearSelection()
            return true
        }
        return false
    }

    function openPalette() { commandPalette.open() }
    /// Search the open chat; with none open, search everything.
    function searchChat() {
        const id = App.session.selectedChatId
        if (id === "" || settingsOpen)
            commandPalette.open()
        else
            commandPalette.openInChat(id, App.session.chat.title)
    }
    function openShortcuts() { shortcuts.open() }
    function openModelPicker() {
        settingsOpen = false
        chatPage.openModelPicker()
    }

    function toggleSidebar() {
        App.prefs.sidebarOpen = !App.prefs.sidebarOpen
    }

    function newChat() {
        settingsOpen = false
        App.session.newChat()
    }

    function openChat(chatId) {
        settingsOpen = false
        App.session.selectChat(chatId)
    }

    /// Every link in the app comes through here. A link to the connected
    /// bridge is handled inside the app, over its own (possibly mutual-TLS)
    /// connection; a browser would have no client certificate to show.
    function openLink(link) {
        const info = App.links.inspect(link)
        if (info.kind === "pair")
            pairSheet.offer(info.link)
        else if (info.kind === "secret")
            secretSheet.offer(info.link)
        else if (info.kind === "bridge")
            downloads.start(info.link)
        else
            App.links.openExternally(info.link)
    }

    function showImage(path, name) { viewer.show(path, name) }
    function startVoice() { voiceScreen.open() }
    function renameChat(chatId, title) { renameSheet.ask(chatId, title) }
    function confirm(title, message, action, onYes) { confirmSheet.ask(title, message, action, onYes) }

    function exportChat(chatId) {
        App.copy(App.session.exportMarkdown(chatId))
        toast(qsTr("Conversation copied as Markdown"))
    }

    function saveChat(chatId) {
        saveDialog.chatId = chatId
        saveDialog.currentFile = App.suggestedFile(App.session.chat.title || "chat", "md")
        saveDialog.open()
    }

    /// A message of the user's own, back in the box to change and send.
    function editMessage(text) { chatPage.setDraft(text) }

    /// Send the last question once more. The earlier answer stays in the
    /// chat: Talon keeps one line of conversation.
    function askAgain() {
        const text = App.session.lastUserMessage()
        if (text !== "")
            App.session.send(text, [], function (ok) { })
    }

    Connections {
        target: App.session
        function onModelPickerRequested() { root.openModelPicker() }
    }
    // The small one-question window; never on a phone.
    Loader {
        id: quickAsk
        active: !App.mobile
        sourceComponent: QuickAsk { main: root }
    }
    Connections {
        target: App
        function onToast(text) { root.toast(text) }
        function onPairLinkReceived(link) { pairSheet.offer(link) }
        function onOpenChatRequested(chatId) { root.openChat(chatId) }
        function onQuickAskRequested() {
            if (quickAsk.item)
                quickAsk.item.present()
            else
                App.showWindow()
        }
    }

    // ── Layout ──────────────────────────────────────────────────────────────

    Loader {
        anchors.fill: parent
        active: !App.store.hasProfile
        visible: active
        sourceComponent: ConnectPage { }
    }

    Item {
        id: shell
        anchors.fill: parent
        visible: App.store.hasProfile

        // Hidden while settings covers it. Covering it is not enough: a tap
        // on a settings row would also reach the chat row underneath, whose
        // handler doesn't care that something opaque is drawn on top.
        Item {
            anchors.fill: parent
            visible: !root.settingsOpen

            // Wide: the chat list sits beside the conversation.
            Sidebar {
                id: sidebar
                visible: !root.narrow
                width: root.sidebarOpen ? Math.min(App.prefs.sidebarWidth, root.width * 0.42) : 0
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                clip: true

                Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            }

            ChatPage {
                id: chatPage
                anchors.left: root.narrow ? parent.left : sidebar.right
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                visible: !root.narrow || App.session.selectedChatId !== ""
            }

            // Narrow: the chat list is the screen until a chat is opened.
            Sidebar {
                anchors.fill: parent
                visible: root.narrow && App.session.selectedChatId === ""
                fullScreen: true
            }
        }

        Loader {
            id: settingsLoader
            anchors.fill: parent
            active: root.settingsOpen
            visible: active
            sourceComponent: SettingsPage { }
        }
    }

    // ── Overlays ────────────────────────────────────────────────────────────

    CommandPalette { id: commandPalette }
    ShortcutsSheet { id: shortcuts }
    ImageViewer { id: viewer }
    VoiceScreen { id: voiceScreen }
    LinkSheets {
        id: linkSheets
        // Aliased below so callers can say pairSheet.offer(…).
    }
    readonly property var pairSheet: linkSheets.pair
    readonly property var secretSheet: linkSheets.secret
    readonly property var downloads: linkSheets.downloads

    Sheet {
        id: renameSheet
        title: qsTr("Rename chat")
        maxWidth: 420
        property string chatId

        function ask(id, current) {
            chatId = id
            nameField.text = current
            open()
            nameField.forceActiveFocus()
            nameField.selectAll()
        }
        function commit() {
            if (nameField.text.trim() !== "")
                App.session.renameChat(chatId, nameField.text)
            close()
        }

        TField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("Chat name")
            onAccepted: renameSheet.commit()
        }
        footer: [
            TButton { text: qsTr("Rename"); kind: "primary"; onClicked: renameSheet.commit() },
            TButton { text: qsTr("Cancel"); kind: "ghost"; onClicked: renameSheet.close() }
        ]
    }

    Sheet {
        id: confirmSheet
        maxWidth: 420
        property string message
        property string action
        property var onYes: null

        function ask(t, m, a, yes) {
            title = t
            message = m
            action = a
            onYes = yes
            open()
        }

        Text {
            text: confirmSheet.message
            color: Theme.textSecondary
            font.pixelSize: Theme.body
            wrapMode: Text.Wrap
            lineHeight: 1.25
            Layout.fillWidth: true
        }
        footer: [
            TButton {
                text: confirmSheet.action
                kind: "danger"
                onClicked: {
                    const run = confirmSheet.onYes
                    confirmSheet.close()
                    if (run)
                        run()
                }
            },
            TButton { text: qsTr("Cancel"); kind: "ghost"; onClicked: confirmSheet.close() }
        ]
    }

    // A mesh command waiting for the person at this device to allow it.
    Sheet {
        id: approvalSheet
        title: qsTr("Allow this command?")
        dismissable: false
        maxWidth: 460
        readonly property var request: App.approvals.length > 0 ? App.approvals[0] : null
        visible: request !== null && !App.lock.locked

        Text {
            text: qsTr("Talon is asking to run “%1” on this device.")
                  .arg(approvalSheet.request ? approvalSheet.request.command : "")
            color: Theme.text
            font.pixelSize: Theme.body
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Rectangle {
            visible: detail.text !== ""
            Layout.fillWidth: true
            implicitHeight: detail.implicitHeight + 20
            radius: Theme.radius
            color: Theme.sunken

            Text {
                id: detail
                anchors.fill: parent
                anchors.margins: 10
                text: approvalSheet.request ? approvalSheet.request.detail : ""
                color: Theme.text
                font.family: Theme.mono
                font.pixelSize: Theme.small + 1
                wrapMode: Text.WrapAnywhere
            }
        }
        footer: [
            TButton {
                text: qsTr("Allow")
                kind: "primary"
                onClicked: App.resolveApproval(approvalSheet.request.id, true)
            },
            TButton {
                text: qsTr("Deny")
                onClicked: App.resolveApproval(approvalSheet.request.id, false)
            }
        ]
    }

    FileDialog {
        id: saveDialog
        property string chatId
        title: qsTr("Save the conversation")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Markdown (*.md)"), qsTr("Text (*.txt)")]
        defaultSuffix: "md"
        onAccepted: {
            root.toast(App.saveText(selectedFile, App.session.exportMarkdown(chatId))
                       ? qsTr("Conversation saved") : qsTr("Couldn't write that file"))
        }
    }

    Toaster { id: toaster }

    LockScreen { }

    // ── Keyboard ────────────────────────────────────────────────────────────

    Shortcut { sequences: ["Ctrl+K"]; enabled: !App.lock.locked; onActivated: commandPalette.open() }
    Shortcut { sequences: [StandardKey.Find]; enabled: !App.lock.locked; onActivated: root.searchChat() }
    Shortcut { sequences: ["Ctrl+N"]; enabled: !App.lock.locked; onActivated: root.newChat() }
    Shortcut { sequences: ["Ctrl+,"]; enabled: !App.lock.locked; onActivated: root.openSettings() }
    Shortcut { sequences: ["Ctrl+B"]; enabled: !App.lock.locked; onActivated: root.toggleSidebar() }
    Shortcut { sequences: ["Ctrl+/"]; enabled: !App.lock.locked; onActivated: shortcuts.open() }
    Shortcut { sequences: ["Ctrl+L"]; enabled: App.lock.enabled; onActivated: App.lock.lock() }
    Shortcut { sequences: ["Ctrl+Q"]; onActivated: App.quit() }
    Shortcut {
        sequences: ["Escape"]
        enabled: root.settingsOpen
        onActivated: root.settingsOpen = false
    }
    Shortcut {
        sequences: ["Ctrl+Shift+E"]
        enabled: App.session.selectedChatId !== ""
        onActivated: root.exportChat(App.session.selectedChatId)
    }
    Shortcut {
        sequences: ["Ctrl+."]
        enabled: App.session.turn.active
        onActivated: App.session.interrupt()
    }
}
