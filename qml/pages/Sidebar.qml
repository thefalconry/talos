// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import Talos.Native

// The chat list: new chat, search, history grouped by day, and the
// connection's state at the bottom.
Rectangle {
    id: root

    property bool fullScreen: false
    readonly property var win: C.ApplicationWindow.window

    color: fullScreen ? Theme.bg : Theme.sidebar

    Rectangle {
        visible: !root.fullScreen
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: root.fullScreen ? 0 : 1
        spacing: 0

        // Brand row
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 8
            Layout.topMargin: 10
            spacing: 8

            BrandMark { size: 24 }
            Text {
                text: App.session.botName
                color: Theme.text
                font.pixelSize: Theme.title
                font.weight: Font.DemiBold
                font.family: Theme.serif
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            IconButton {
                iconName: "search"
                tip: App.keys(qsTr("Search and commands  (Ctrl+K)"))
                onClicked: root.win.openPalette()
            }
            IconButton {
                visible: !root.fullScreen
                iconName: "sidebar"
                tip: App.keys(qsTr("Hide sidebar  (Ctrl+B)"))
                onClicked: root.win.toggleSidebar()
            }
        }

        TButton {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 10
            kind: "secondary"
            iconName: "plus"
            text: qsTr("New chat")
            enabled: root.win.connected
            onClicked: root.win.newChat()
        }

        TField {
            id: filter
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 8
            implicitHeight: 34
            placeholderText: qsTr("Filter chats")
            leftPadding: 32
            onTextChanged: App.session.chats.filter = text

            Icon {
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                size: 15
                color: Theme.textTertiary
            }
            Keys.onEscapePressed: text = ""
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 6
            clip: true
            model: App.session.chats
            boundsBehavior: root.fullScreen ? Flickable.DragAndOvershootBounds
                                            : Flickable.StopAtBounds
            topMargin: 2
            bottomMargin: 8

            section.property: "section"
            section.delegate: Item {
                required property string section
                width: list.width
                height: 30

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 5
                    text: parent.section
                    color: Theme.textTertiary
                    font.pixelSize: Theme.small
                    font.weight: Font.Medium
                }
            }

            delegate: Item {
                id: row
                required property string chatId
                required property string title
                required property string preview
                required property bool unread
                required property bool running
                required property bool queued
                required property bool pinned
                required property bool archived
                readonly property bool current: chatId === App.session.selectedChatId

                Accessible.role: Accessible.ListItem
                Accessible.name: unread ? qsTr("%1, unread").arg(title) : title
                Accessible.description: preview
                Accessible.selected: current
                Accessible.onPressAction: root.win.openChat(chatId)
                activeFocusOnTab: true
                Keys.onReturnPressed: root.win.openChat(chatId)
                Keys.onSpacePressed: root.win.openChat(chatId)
                Keys.onMenuPressed: menu.popup()

                width: list.width
                height: root.fullScreen ? 58 : 38

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.topMargin: 1
                    anchors.bottomMargin: 1
                    radius: 9
                    color: row.current && !root.fullScreen ? Theme.selected
                         : hover.hovered ? Theme.hover : "transparent"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 12
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: row.title
                            color: Theme.text
                            font.pixelSize: Theme.body
                            font.weight: row.unread ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: root.fullScreen && row.preview !== ""
                            text: row.preview
                            color: Theme.textSecondary
                            font.pixelSize: Theme.small + 1
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Spinner {
                        visible: row.running
                        size: 13
                        color: Theme.accent
                    }
                    Rectangle {
                        visible: row.unread && !row.running
                        width: 8
                        height: 8
                        radius: 4
                        color: Theme.accent
                    }
                    IconButton {
                        visible: hover.hovered || menu.visible
                        size: 26
                        iconSize: 16
                        iconName: "more"
                        iconWeight: 2.6
                        tip: qsTr("Chat options")
                        onClicked: menu.popup()
                    }
                }

                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: root.win.openChat(row.chatId)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: menu.popup()
                }
                TapHandler {
                    acceptedDevices: PointerDevice.TouchScreen
                    onLongPressed: menu.popup()
                }

                TMenu {
                    id: menu
                    TMenuItem {
                        text: qsTr("Rename")
                        iconName: "edit"
                        onTriggered: root.win.renameChat(row.chatId, row.title)
                    }
                    TMenuItem {
                        visible: !row.archived
                        height: visible ? implicitHeight : 0
                        text: row.pinned ? qsTr("Unpin") : qsTr("Pin to the top")
                        iconName: "pin"
                        onTriggered: App.session.setPinned(row.chatId, !row.pinned)
                    }
                    TMenuItem {
                        text: row.archived ? qsTr("Move out of the archive") : qsTr("Archive")
                        iconName: "folder"
                        onTriggered: App.session.setArchived(row.chatId, !row.archived)
                    }
                    TMenuItem {
                        text: qsTr("Copy as Markdown")
                        iconName: "copy"
                        enabled: row.current
                        onTriggered: root.win.exportChat(row.chatId)
                    }
                    C.MenuSeparator {
                        contentItem: Rectangle { implicitHeight: 1; color: Theme.border }
                    }
                    TMenuItem {
                        text: qsTr("Delete")
                        iconName: "trash"
                        danger: true
                        onTriggered: root.win.confirm(
                            qsTr("Delete this chat?"),
                            qsTr("“%1” and its history are removed from Talon. This can't be undone.").arg(row.title),
                            qsTr("Delete"), () => App.session.deleteChat(row.chatId))
                    }
                }
            }

            C.ScrollBar.vertical: C.ScrollBar { }

            // Nothing to list.
            Column {
                anchors.centerIn: parent
                width: parent.width - 48
                spacing: 6
                visible: list.count === 0

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Theme.textSecondary
                    font.pixelSize: Theme.body
                    text: filter.text !== "" ? qsTr("No chat matches “%1”").arg(filter.text)
                        : App.session.chats.archived ? qsTr("Nothing is archived")
                        : root.win.connected ? qsTr("No chats yet")
                        : qsTr("Chats appear here once Talon is connected")
                }
            }
        }

        // The archive: chats put out of the way, one tap from the list.
        Item {
            visible: App.session.archivedCount > 0 || App.session.chats.archived
            Layout.fillWidth: true
            implicitHeight: 34

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.bottomMargin: 4
                radius: 9
                color: archiveHover.hovered ? Theme.hover : "transparent"

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 12
                    spacing: 9
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: App.session.chats.archived ? "arrow-left" : "folder"
                        size: 15
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: App.session.chats.archived
                              ? qsTr("Back to chats")
                              : qsTr("Archived (%1)").arg(App.session.archivedCount)
                        color: Theme.textSecondary
                        font.pixelSize: Theme.small + 1
                    }
                }
                HoverHandler { id: archiveHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: App.session.chats.archived = !App.session.chats.archived }
            }
        }

        // Connection + settings
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 6
            Layout.bottomMargin: 8
            spacing: 4

            ConnectionPill {
                Layout.fillWidth: true
                onClicked: root.win.openSettings("connection")
            }
            IconButton {
                iconName: "settings"
                tip: App.keys(qsTr("Settings  (Ctrl+,)"))
                onClicked: root.win.openSettings()

                Rectangle {
                    visible: App.updater.available
                    width: 8
                    height: 8
                    radius: 4
                    color: Theme.accent
                    x: parent.width - 11
                    y: 5
                }
            }
        }
    }
}
