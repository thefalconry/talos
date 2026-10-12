// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts

Sheet {
    id: sheet
    title: qsTr("Keyboard shortcuts")
    maxWidth: 440

    readonly property var keys: [
        ["Ctrl+K", qsTr("Search, jump to a chat, run a command")],
        ["Ctrl+F", qsTr("Search the open chat")],
        ["Ctrl+N", qsTr("New chat")],
        ["Ctrl+B", qsTr("Show or hide the sidebar")],
        ["Ctrl+,", qsTr("Settings")],
        ["Enter", qsTr("Send (Shift+Enter for a new line)")],
        ["Ctrl+Enter", qsTr("Send, whatever the Enter setting")],
        ["/", qsTr("Start a slash command; Tab completes")],
        ["Esc  ·  Ctrl+.", qsTr("Stop the current response")],
        ["Ctrl+V", qsTr("Paste an image as an attachment")],
        ["Ctrl+Shift+E", qsTr("Copy the conversation as Markdown")],
        ["Ctrl+L", qsTr("Lock the app")],
        ["Ctrl+/", qsTr("This list")],
        ["Ctrl+Q", qsTr("Quit")]
    ]

    Repeater {
        model: sheet.keys

        RowLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 14

            Rectangle {
                implicitWidth: Math.max(key.implicitWidth + 16, 118)
                implicitHeight: 26
                radius: 7
                color: Theme.sunken
                border.width: 1
                border.color: Theme.border

                Text {
                    id: key
                    anchors.centerIn: parent
                    text: parent.parent.modelData[0]
                    color: Theme.text
                    font.family: Theme.mono
                    font.pixelSize: Theme.small
                }
            }
            Text {
                text: parent.modelData[1]
                color: Theme.textSecondary
                font.pixelSize: Theme.body
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }
}
