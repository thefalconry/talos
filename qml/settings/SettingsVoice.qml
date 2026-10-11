// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

Pane {
    id: pane

    Component.onCompleted: App.voice.applySettings()

    Text {
        visible: !App.voice.canSpeak
        text: qsTr("No text-to-speech engine was found on this device, so replies can't be read aloud.")
        color: Theme.textSecondary
        font.pixelSize: Theme.body
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    Card {
        visible: App.voice.canSpeak
        title: qsTr("Reading replies aloud")

        SettingRow {
            first: true
            title: qsTr("Read new replies automatically")
            subtitle: qsTr("In the chat you have open, while the app is in front.")
            TSwitch {
                checked: App.prefs.voiceAutoSpeak
                onToggled: App.prefs.voiceAutoSpeak = checked
            }
        }
        SettingRow {
            title: qsTr("Speed")
            Segmented {
                value: App.prefs.voiceRate
                options: [
                    { value: -0.3, label: qsTr("Slower") }, { value: 0.0, label: qsTr("Normal") },
                    { value: 0.25, label: qsTr("Faster") }, { value: 0.5, label: qsTr("Fast") }
                ]
                onChosen: value => App.prefs.voiceRate = value
            }
        }
        SettingRow {
            title: qsTr("Voice")
            subtitle: App.prefs.voiceName === "" ? qsTr("The system's default voice") : App.prefs.voiceName
            TButton {
                compact: true
                text: qsTr("Choose")
                enabled: App.voice.voices.length > 0
                onClicked: voiceMenu.popup()

                TMenu {
                    id: voiceMenu
                    TMenuItem {
                        text: qsTr("System default")
                        checkable: true
                        checked: App.prefs.voiceName === ""
                        onTriggered: App.prefs.voiceName = ""
                    }
                    Repeater {
                        model: App.voice.voices
                        TMenuItem {
                            required property var modelData
                            text: modelData.name
                            checkable: true
                            checked: App.prefs.voiceName === modelData.name
                            onTriggered: App.prefs.voiceName = modelData.name
                        }
                    }
                }
            }
        }
        SettingRow {
            clickable: true
            iconName: "volume"
            title: App.voice.speaking ? qsTr("Stop") : qsTr("Hear a sample")
            onClicked: App.voice.speaking ? App.voice.stop()
                                          : App.voice.speak(qsTr("Hello. This is how replies will sound."))
        }
    }

    Card {
        title: qsTr("Speaking to Talon")

        SettingRow {
            first: true
            iconName: "mic"
            title: App.voice.canListen ? qsTr("Speech input is available")
                                       : qsTr("Speech input isn't available here")
            subtitle: App.voice.canListen
                      ? qsTr("Uses this device's own speech recogniser. Start voice mode from the microphone in a chat.")
                      : qsTr("It relies on the platform's speech recogniser, which this app can only reach on Android. Voice mode here reads replies aloud and takes typed turns.")
        }
    }
}
