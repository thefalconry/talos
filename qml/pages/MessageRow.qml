// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import Talos.Native

// One message. The person's own messages sit in a bubble on the right; the
// assistant's run the width of the reading column with no chrome, so long
// answers read like a page rather than a chat log.
Item {
    id: row

    required property string messageId
    required property string role
    required property string text
    required property var ts
    required property var blocks
    required property var images
    required property var files
    required property var buttons
    required property var reactions
    required property var tools
    required property string stats
    required property string day

    property int column: Theme.column
    readonly property var win: C.ApplicationWindow.window
    readonly property bool mine: role === "user"
    readonly property bool note: role === "system"

    implicitHeight: body.implicitHeight

    Column {
        id: body
        width: row.column
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 10

        // First message of a day.
        Text {
            visible: row.day !== ""
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: row.day
            color: Theme.textTertiary
            font.pixelSize: Theme.small
            bottomPadding: 4
        }

        // A local or daemon notice.
        Row {
            visible: row.note
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 7

            Icon {
                name: "info"
                size: 14
                color: Theme.textTertiary
                anchors.top: parent.top
                anchors.topMargin: 2
            }
            Text {
                width: Math.min(implicitWidth, row.column - 40)
                text: row.text
                color: Theme.textSecondary
                font.pixelSize: Theme.small + 1
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignLeft
            }
        }

        // Tools the assistant used on the way to this reply.
        Column {
            id: toolBlock
            visible: !row.mine && !row.note && row.tools.length > 0
            width: parent.width
            property bool open: false

            Rectangle {
                width: toolSummary.implicitWidth + 20
                height: 30
                radius: 8
                color: toolTap.pressed ? Theme.pressed : toolHover.hovered ? Theme.hover : "transparent"

                Row {
                    id: toolSummary
                    anchors.centerIn: parent
                    spacing: 7

                    Icon {
                        name: "tool"
                        size: 14
                        color: Theme.textTertiary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: row.tools.length === 1
                              ? qsTr("Used %1").arg(row.tools[0].label)
                              : qsTr("Used %1 tools").arg(row.tools.length)
                        color: Theme.textSecondary
                        font.pixelSize: Theme.small + 1
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Icon {
                        name: toolBlock.open ? "chevron-up" : "chevron-down"
                        size: 13
                        color: Theme.textTertiary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                HoverHandler { id: toolHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { id: toolTap; onTapped: toolBlock.open = !toolBlock.open }
            }
            Loader {
                width: parent.width
                active: toolBlock.open
                visible: active
                sourceComponent: ToolList { tools: row.tools }
            }
        }

        // Attached images.
        Flow {
            visible: row.images.length > 0
            width: parent.width
            spacing: 8
            layoutDirection: row.mine ? Qt.RightToLeft : Qt.LeftToRight

            Repeater {
                model: row.images

                RemoteImage {
                    id: shot
                    required property var modelData
                    readonly property real box: Math.min(row.column,
                                                         row.images.length > 1 ? 220 : 380)
                    path: modelData.url
                    name: modelData.name
                    width: box
                    height: Math.min(box / aspect, 380)
                    fillMode: Image.PreserveAspectCrop

                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: row.win.showImage(shot.path, shot.name) }
                }
            }
        }

        // Attached files.
        Flow {
            visible: row.files.length > 0
            width: parent.width
            spacing: 8
            layoutDirection: row.mine ? Qt.RightToLeft : Qt.LeftToRight

            Repeater {
                model: row.files

                Rectangle {
                    id: chip
                    required property var modelData
                    width: Math.min(chipRow.implicitWidth + 24, row.column)
                    height: 46
                    radius: Theme.radius
                    color: chipHover.hovered ? Theme.hover : Theme.surface
                    border.width: 1
                    border.color: Theme.border

                    RowLayout {
                        id: chipRow
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 10

                        Icon { name: "file"; size: 18; color: Theme.textSecondary }
                        ColumnLayout {
                            spacing: 1
                            Layout.fillWidth: true

                            Text {
                                text: chip.modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.small + 1
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                                Layout.maximumWidth: 260
                            }
                            Text {
                                text: chip.modelData.sizeLabel
                                visible: text !== ""
                                color: Theme.textTertiary
                                font.pixelSize: Theme.small
                            }
                        }
                    }
                    HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: App.media.open(chip.modelData.url, chip.modelData.name)
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: fileMenu.popup()
                    }
                    TMenu {
                        id: fileMenu
                        TMenuItem {
                            text: qsTr("Open")
                            iconName: "external"
                            onTriggered: App.media.open(chip.modelData.url, chip.modelData.name)
                        }
                        TMenuItem {
                            text: qsTr("Save to Downloads")
                            iconName: "download"
                            onTriggered: App.media.saveTo(chip.modelData.url, chip.modelData.name,
                                                          "file://" + App.media.downloadsDir())
                        }
                    }
                }
            }
        }

        // The person's words.
        Rectangle {
            visible: row.mine && row.text !== ""
            anchors.right: parent.right
            width: Math.min(mineText.implicitWidth + 28, row.column * 0.84)
            height: mineText.implicitHeight + 20
            radius: 16
            color: Theme.userBubble

            TextEdit {
                id: mineText
                x: 14
                y: 10
                width: Math.min(implicitWidth, row.column * 0.84 - 28)
                text: row.text
                readOnly: true
                selectByMouse: !App.mobile
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                color: Theme.text
                font.pixelSize: Theme.reading
                selectionColor: Theme.accentSoft
                selectedTextColor: Theme.text
            }
        }

        // The assistant's reply.
        MarkdownView {
            visible: !row.mine && !row.note && row.text !== ""
            width: parent.width
            blocks: row.blocks
            onLinkActivated: link => row.win.openLink(link)
        }

        // Inline buttons. A button with a link opens it; one without is a
        // quick reply and sends its own label.
        Column {
            visible: !row.note && row.buttons.length > 0
            width: parent.width
            spacing: 6

            Repeater {
                model: row.buttons

                Flow {
                    id: buttonRow
                    required property var modelData
                    width: parent.width
                    spacing: 6

                    Repeater {
                        model: buttonRow.modelData

                        TButton {
                            required property var modelData
                            text: modelData.text || ""
                            iconName: modelData.url ? "external" : ""
                            compact: true
                            onClicked: modelData.url ? row.win.openLink(modelData.url)
                                                     : App.session.send(modelData.text, [], null)
                        }
                    }
                }
            }
        }

        Row {
            visible: row.reactions.length > 0
            anchors.right: row.mine ? parent.right : undefined
            spacing: 4

            Repeater {
                model: row.reactions

                Rectangle {
                    required property string modelData
                    width: emoji.implicitWidth + 14
                    height: 26
                    radius: 13
                    color: Theme.sunken
                    border.width: 1
                    border.color: Theme.border

                    Text {
                        id: emoji
                        anchors.centerIn: parent
                        text: parent.modelData
                        font.pixelSize: Theme.body
                    }
                }
            }
        }

        // Quiet footer: actions on hover, turn stats, time.
        Row {
            visible: !row.note
            anchors.right: row.mine ? parent.right : undefined
            height: 24
            spacing: 2
            opacity: hover.hovered || App.mobile ? 1 : 0.0

            Behavior on opacity { NumberAnimation { duration: 100 } }

            IconButton {
                id: copyButton
                property bool done: false
                size: 24
                iconSize: 14
                iconName: done ? "check" : "copy"
                tip: qsTr("Copy")
                onClicked: {
                    App.copy(row.text)
                    done = true
                    copyReset.restart()
                }
                Timer { id: copyReset; interval: 1500; onTriggered: copyButton.done = false }
            }
            IconButton {
                visible: !row.mine && App.voice.canSpeak
                size: 24
                iconSize: 14
                iconName: "volume"
                tip: App.voice.speaking ? qsTr("Stop reading") : qsTr("Read aloud")
                onClicked: App.voice.speaking ? App.voice.stop() : App.voice.speak(row.text)
            }
            Text {
                leftPadding: 6
                anchors.verticalCenter: parent.verticalCenter
                text: (row.stats !== "" ? row.stats + " · " : "") + App.timeLabel(row.ts)
                color: Theme.textTertiary
                font.pixelSize: Theme.small
            }
        }
    }

    HoverHandler { id: hover }
}
