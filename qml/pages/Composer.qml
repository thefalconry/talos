// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Dialogs
import QtQuick.Layouts
import Talos.Native

// Where messages are written: text, attachments, slash commands, the queued
// follow-up, and send / stop.
Item {
    id: composer

    signal modelRequested()

    readonly property var chat: App.session.chat
    readonly property bool running: App.session.turn.active
    readonly property bool uploading: uploads.some(u => u.state === "uploading")
    readonly property bool canSend: (input.text.trim() !== ""
                                     || uploads.some(u => u.state === "done"))
                                    && !uploading && !sending
    property bool sending: false
    /// [{id, name, progress, state: uploading|done|failed, error, attachment}]
    property var uploads: []
    /// Unsent text per chat, so switching chats never loses a draft.
    property var drafts: ({})
    property string draftChat: App.session.selectedChatId

    readonly property var commandMatches: {
        const t = input.text
        if (!t.startsWith("/") || /\s/.test(t))
            return []
        const typed = t.slice(1).toLowerCase()
        return App.session.commands.filter(c => c.name.startsWith(typed)).slice(0, 8)
    }
    property int commandIndex: 0
    onCommandMatchesChanged: commandIndex = 0

    implicitHeight: column.implicitHeight

    function attach(url) {
        const id = App.session.upload(url)
        uploads = uploads.concat([{ id: id, name: App.fileName(url), progress: 0,
                                    state: "uploading", error: "", attachment: null }])
    }

    function patchUpload(id, change) {
        uploads = uploads.map(u => u.id === id ? Object.assign({}, u, change) : u)
    }

    function removeUpload(id) {
        App.session.cancelUpload(id)
        uploads = uploads.filter(u => u.id !== id)
    }

    function send() {
        if (!canSend)
            return
        const text = input.text
        const files = uploads.filter(u => u.state === "done").map(u => u.attachment)
        sending = true
        App.session.send(text, files, function (ok) {
            composer.sending = false
            if (!ok)
                return
            // Only now is it safe to let go of the draft.
            if (input.text === text)
                input.clear()
            composer.uploads = composer.uploads.filter(u => u.state !== "done")
        })
    }

    function completeCommand() {
        const c = commandMatches[commandIndex]
        if (!c)
            return false
        input.text = "/" + c.name + (c.args ? " " : "")
        input.cursorPosition = input.length
        return true
    }

    function focusInput() { input.forceActiveFocus() }

    Connections {
        target: App.session
        function onUploadProgress(id, fraction) {
            composer.patchUpload(id, { progress: fraction })
        }
        function onUploadFinished(id, attachment) {
            composer.patchUpload(id, { state: "done", progress: 1, attachment: attachment })
        }
        function onUploadFailed(id, error) {
            composer.patchUpload(id, { state: "failed", error: error })
        }
        function onSelectionChanged() {
            const next = App.session.selectedChatId
            composer.drafts[composer.draftChat] = input.text
            composer.draftChat = next
            input.text = composer.drafts[next] || ""
            input.cursorPosition = input.length
            if (!App.mobile)
                input.forceActiveFocus()
        }
    }
    Connections {
        target: App.voice
        function onHeard(text) {
            input.insert(input.cursorPosition, (input.length > 0 ? " " : "") + text)
        }
        function onListenFailed(reason) { App.toast(reason) }
    }

    FileDialog {
        id: picker
        title: qsTr("Attach files")
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            for (const url of selectedFiles)
                composer.attach(url)
        }
    }

    ColumnLayout {
        id: column
        width: parent.width
        spacing: 8

        // Slash commands: the daemon's own, or on an older daemon the few
        // the app can carry out itself.
        Rectangle {
            visible: composer.commandMatches.length > 0
            Layout.fillWidth: true
            implicitHeight: commandList.implicitHeight + 10
            radius: 12
            color: Theme.raised
            border.width: 1
            border.color: Theme.border

            Column {
                id: commandList
                x: 5
                y: 5
                width: parent.width - 10

                Repeater {
                    model: composer.commandMatches

                    Rectangle {
                        id: commandRow
                        required property var modelData
                        required property int index
                        width: commandList.width
                        height: 34
                        radius: 8
                        color: index === composer.commandIndex ? Theme.hover : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10

                            Text {
                                text: "/" + commandRow.modelData.name
                                color: Theme.text
                                font.family: Theme.mono
                                font.pixelSize: Theme.small + 1
                            }
                            Text {
                                visible: text !== ""
                                text: commandRow.modelData.args || ""
                                color: Theme.textTertiary
                                font.family: Theme.mono
                                font.pixelSize: Theme.small
                                elide: Text.ElideRight
                                Layout.maximumWidth: commandList.width * 0.35
                            }
                            Text {
                                text: commandRow.modelData.description || ""
                                color: Theme.textSecondary
                                font.pixelSize: Theme.small + 1
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                visible: commandRow.modelData.admin === true
                                text: qsTr("operator")
                                color: Theme.textTertiary
                                font.pixelSize: Theme.small
                            }
                        }
                        HoverHandler { onHoveredChanged: if (hovered) composer.commandIndex = commandRow.index }
                        TapHandler {
                            onTapped: {
                                composer.commandIndex = commandRow.index
                                composer.completeCommand()
                                input.forceActiveFocus()
                            }
                        }
                    }
                }
            }
        }

        // The follow-up parked while a turn runs. It lives on the daemon, so
        // every device sees and can change the same one.
        Rectangle {
            visible: composer.chat.hasQueued === true
            Layout.fillWidth: true
            implicitHeight: 40
            radius: 12
            color: Theme.sunken
            border.width: 1
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 4
                spacing: 8

                Icon { name: "clock"; size: 15; color: Theme.textTertiary }
                Text {
                    text: qsTr("Queued: %1").arg(composer.chat.queuedText || "")
                          + (composer.chat.queuedAttachments > 0
                             ? qsTr("  (+%n file(s))", "", composer.chat.queuedAttachments) : "")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.small + 1
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                TButton {
                    compact: true
                    kind: "ghost"
                    text: qsTr("Edit")
                    onClicked: {
                        input.text = composer.chat.queuedText || ""
                        input.cursorPosition = input.length
                        App.session.editQueued("")
                        input.forceActiveFocus()
                    }
                }
                IconButton {
                    size: 28
                    iconSize: 15
                    iconName: "x"
                    tip: qsTr("Cancel the queued message")
                    onClicked: App.session.editQueued("")
                }
            }
        }

        Rectangle {
            id: box
            Layout.fillWidth: true
            implicitHeight: inner.implicitHeight + 16
            radius: 20
            color: Theme.surface
            border.width: input.activeFocus ? 1.5 : 1
            border.color: input.activeFocus ? Theme.borderStrong : Theme.border

            ColumnLayout {
                id: inner
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 8
                spacing: 6

                // Files staged for this message.
                Flow {
                    visible: composer.uploads.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    Layout.topMargin: 4
                    spacing: 6

                    Repeater {
                        model: composer.uploads

                        Rectangle {
                            id: staged
                            required property var modelData
                            width: Math.min(stagedRow.implicitWidth + 16, 260)
                            height: 34
                            radius: 9
                            color: Theme.sunken
                            border.width: 1
                            border.color: modelData.state === "failed" ? Theme.danger : Theme.border
                            clip: true

                            Rectangle {
                                visible: staged.modelData.state === "uploading"
                                width: parent.width * staged.modelData.progress
                                height: parent.height
                                color: Theme.accentSoft
                            }
                            RowLayout {
                                id: stagedRow
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 2
                                spacing: 6

                                Icon {
                                    name: staged.modelData.state === "failed" ? "alert" : "file"
                                    size: 15
                                    color: staged.modelData.state === "failed" ? Theme.danger
                                                                               : Theme.textSecondary
                                }
                                Text {
                                    text: staged.modelData.state === "failed"
                                          ? staged.modelData.error : staged.modelData.name
                                    color: Theme.text
                                    font.pixelSize: Theme.small + 1
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                    Layout.maximumWidth: 190
                                }
                                IconButton {
                                    size: 26
                                    iconSize: 13
                                    iconName: "x"
                                    tip: qsTr("Remove")
                                    onClicked: composer.removeUpload(staged.modelData.id)
                                }
                            }
                        }
                    }
                }

                Flickable {
                    id: scroller
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    implicitHeight: Math.min(Math.max(input.implicitHeight, 30), 220)
                    contentHeight: input.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    C.TextArea.flickable: C.TextArea {
                        id: input
                        placeholderText: composer.running
                                         ? qsTr("Queue a follow-up…")
                                         : qsTr("Message %1").arg(App.session.botName)
                        placeholderTextColor: Theme.textTertiary
                        color: Theme.text
                        font.pixelSize: Theme.reading
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        selectionColor: Theme.accentSoft
                        selectedTextColor: Theme.text
                        padding: 4
                        background: null

                        Keys.onPressed: event => {
                            const popup = composer.commandMatches.length > 0
                            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                const ctrl = event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)
                                if (event.modifiers & Qt.ShiftModifier)
                                    return
                                // "/he" + Enter completes; a whole command sends.
                                if (popup && !ctrl
                                        && "/" + composer.commandMatches[composer.commandIndex].name !== text
                                        && composer.completeCommand()) {
                                    event.accepted = true
                                    return
                                }
                                if (ctrl || (App.prefs.sendOnEnter && !App.mobile)) {
                                    composer.send()
                                    event.accepted = true
                                }
                            } else if (event.key === Qt.Key_Tab && popup) {
                                event.accepted = composer.completeCommand()
                            } else if (event.key === Qt.Key_Down && popup) {
                                composer.commandIndex = (composer.commandIndex + 1) % composer.commandMatches.length
                                event.accepted = true
                            } else if (event.key === Qt.Key_Up && popup) {
                                composer.commandIndex = (composer.commandIndex + composer.commandMatches.length - 1)
                                                        % composer.commandMatches.length
                                event.accepted = true
                            } else if (event.matches(StandardKey.Paste) && App.clipboardHasImage()) {
                                const id = App.session.uploadClipboardImage()
                                if (id !== "") {
                                    composer.uploads = composer.uploads.concat([{
                                        id: id, name: qsTr("Pasted image"), progress: 0,
                                        state: "uploading", error: "", attachment: null }])
                                    event.accepted = true
                                }
                            } else if (event.key === Qt.Key_Escape && composer.running) {
                                App.session.interrupt()
                                event.accepted = true
                            }
                        }
                    }
                    C.ScrollBar.vertical: C.ScrollBar { }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    IconButton {
                        iconName: "paperclip"
                        tip: qsTr("Attach files")
                        enabled: App.session.conn === Session.Connected
                        onClicked: picker.open()
                    }
                    // The chat's model; click to change it or the effort.
                    Rectangle {
                        implicitWidth: modelRow.implicitWidth + 20
                        implicitHeight: 30
                        radius: 9
                        color: modelTap.pressed ? Theme.pressed : modelHover.hovered ? Theme.hover : "transparent"
                        Layout.maximumWidth: 240

                        Row {
                            id: modelRow
                            anchors.centerIn: parent
                            spacing: 5

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(implicitWidth, 190)
                                elide: Text.ElideRight
                                color: Theme.textSecondary
                                font.pixelSize: Theme.small + 1
                                text: {
                                    const id = composer.chat.model || ""
                                    for (const m of App.session.models) {
                                        if (m.id === id)
                                            return m.displayName
                                    }
                                    return id !== "" && id !== "default"
                                         ? id : (App.session.status.model || qsTr("Model"))
                                }
                            }
                            Text {
                                visible: (composer.chat.effort || "") !== ""
                                         && composer.chat.effort !== "adaptive"
                                anchors.verticalCenter: parent.verticalCenter
                                text: "· " + (composer.chat.effort || "")
                                color: Theme.textTertiary
                                font.pixelSize: Theme.small + 1
                            }
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "chevron-down"
                                size: 13
                                color: Theme.textTertiary
                            }
                        }
                        HoverHandler { id: modelHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { id: modelTap; onTapped: composer.modelRequested() }
                    }
                    Item { Layout.fillWidth: true }
                    IconButton {
                        visible: App.voice.canListen
                        iconName: "mic"
                        tip: qsTr("Dictate")
                        onClicked: App.voice.listen()
                    }
                    IconButton {
                        visible: composer.running
                        iconName: "stop"
                        filled: true
                        tip: qsTr("Stop  (Esc)")
                        onClicked: App.session.interrupt()
                    }
                    IconButton {
                        accent: true
                        size: 32
                        iconSize: 17
                        iconName: "arrow-up"
                        tip: composer.running ? qsTr("Queue to send next") : qsTr("Send")
                        enabled: composer.canSend
                        onClicked: composer.send()
                    }
                }
            }
        }
    }
}
