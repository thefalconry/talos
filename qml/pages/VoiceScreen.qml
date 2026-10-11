// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import Talos.Native

// Hands-free conversation: listen, send, read the reply aloud, listen again.
// Listening needs the platform's speech recogniser, which Qt only reaches on
// Android; elsewhere this screen reads replies and takes typed turns.
C.Popup {
    id: screen

    readonly property var turn: App.session.turn
    readonly property string phase: App.voice.listening ? "listening"
                                  : turn.active ? "thinking"
                                  : App.voice.speaking ? "speaking" : "idle"
    property string lastHeard
    property bool handsFree: App.voice.canListen

    parent: C.Overlay.overlay
    modal: true
    focus: true
    padding: 0
    width: parent.width
    height: parent.height
    closePolicy: C.Popup.CloseOnEscape

    onOpened: {
        lastHeard = ""
        if (handsFree)
            App.voice.listen()
    }
    onClosed: App.voice.stop()

    Connections {
        target: App.voice
        enabled: screen.visible
        function onHeard(text) {
            screen.lastHeard = text
            App.session.send(text, [], null)
        }
        function onFinishedSpeaking() {
            if (screen.handsFree && !screen.turn.active)
                App.voice.listen()
        }
    }
    Connections {
        target: App.session
        enabled: screen.visible
        function onMessageArrived(chatId, title, text, fromAssistant) {
            if (fromAssistant && chatId === App.session.selectedChatId)
                App.voice.speak(text)
        }
    }

    background: Rectangle { color: Theme.bg }

    contentItem: Item {
        IconButton {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 12
            iconName: "x"
            tip: qsTr("Leave voice mode")
            onClicked: screen.close()
        }

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 420)
            spacing: 26

            // The orb: still when idle, breathing while something happens.
            Item {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 180
                implicitHeight: 180

                Rectangle {
                    id: halo
                    anchors.centerIn: parent
                    width: 150
                    height: 150
                    radius: 75
                    color: Theme.accentSoft

                    SequentialAnimation on scale {
                        running: screen.phase !== "idle" && screen.visible
                        loops: Animation.Infinite
                        alwaysRunToEnd: true
                        NumberAnimation { to: 1.18; duration: screen.phase === "thinking" ? 520 : 900; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0; duration: screen.phase === "thinking" ? 520 : 900; easing.type: Easing.InOutSine }
                    }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 104
                    height: 104
                    radius: 52
                    color: Theme.accent

                    Icon {
                        anchors.centerIn: parent
                        name: screen.phase === "speaking" ? "volume"
                            : screen.phase === "thinking" ? "spark" : "mic"
                        size: 38
                        weight: 2
                        color: Theme.accentInk
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                color: Theme.text
                font.family: Theme.serif
                font.pixelSize: Theme.fs(24)
                text: screen.phase === "listening" ? qsTr("Listening…")
                    : screen.phase === "thinking" ? qsTr("Thinking…")
                    : screen.phase === "speaking" ? qsTr("Speaking")
                    : App.voice.canListen ? qsTr("Tap the microphone to speak")
                                          : qsTr("Type a message; replies are read aloud")
            }
            Text {
                visible: text !== ""
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                maximumLineCount: 4
                elide: Text.ElideRight
                color: Theme.textSecondary
                font.pixelSize: Theme.body
                text: screen.turn.text !== "" ? screen.turn.text : screen.lastHeard
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 10

                TButton {
                    visible: App.voice.canListen
                    kind: "primary"
                    iconName: "mic"
                    text: qsTr("Speak")
                    enabled: screen.phase === "idle" || screen.phase === "speaking"
                    onClicked: {
                        App.voice.stop()
                        App.voice.listen()
                    }
                }
                TButton {
                    visible: screen.phase === "speaking"
                    iconName: "stop"
                    text: qsTr("Stop reading")
                    onClicked: App.voice.stop()
                }
                TButton {
                    visible: screen.phase === "thinking"
                    iconName: "stop"
                    text: qsTr("Stop")
                    onClicked: App.session.interrupt()
                }
            }
            TField {
                visible: !App.voice.canListen
                Layout.fillWidth: true
                placeholderText: qsTr("Say something to %1").arg(App.session.botName)
                onAccepted: {
                    if (text.trim() === "")
                        return
                    screen.lastHeard = text
                    App.session.send(text, [], null)
                    text = ""
                }
            }
            RowLayout {
                visible: App.voice.canListen
                Layout.alignment: Qt.AlignHCenter
                spacing: 10

                Text {
                    text: qsTr("Keep listening after each reply")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.small + 1
                }
                TSwitch {
                    checked: screen.handsFree
                    onToggled: screen.handsFree = checked
                }
            }
        }
    }
}
