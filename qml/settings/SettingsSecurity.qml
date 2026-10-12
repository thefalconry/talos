// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

Pane {
    id: pane

    Card {
        title: qsTr("App lock")
        caption: qsTr("A passcode in front of the app. It covers the screen, encrypts the cached chats on this device and hides what notifications say. The connection keeps running while locked.")

        SettingRow {
            first: true
            iconName: "lock"
            title: App.lock.enabled ? qsTr("App lock is on") : qsTr("App lock is off")
            subtitle: App.lock.enabled ? "" : qsTr("The passcode is never stored and can't be recovered.")
            TButton {
                compact: true
                kind: App.lock.enabled ? "secondary" : "primary"
                text: App.lock.enabled ? qsTr("Turn off") : qsTr("Set a passcode")
                onClicked: lockSheet.begin(App.lock.enabled ? "disable" : "enable")
            }
        }
        SettingRow {
            visible: App.lock.enabled
            clickable: true
            title: qsTr("Change passcode")
            chevron: true
            onClicked: lockSheet.begin("change")
        }
        SettingRow {
            id: bioRow
            property string problem
            visible: App.lock.enabled
            enabled: App.lock.biometricAvailable || App.lock.biometricEnabled
            title: qsTr("Unlock with %1").arg(App.lock.biometricName.toLowerCase())
            subtitle: problem !== "" ? problem
                    : !App.lock.biometricAvailable && !App.lock.biometricEnabled
                      ? App.lock.biometricUnavailableReason
                    : App.platform === "android"
                      ? qsTr("The passcode still works. The key is held by the device and released only to an enrolled biometric.")
                      : qsTr("The passcode still works. The fingerprint is this app's own check in front of a key kept in your login keyring.")
            TSwitch {
                checked: App.lock.biometricEnabled
                onToggled: {
                    bioRow.problem = ""
                    if (!checked) {
                        App.lock.disableBiometric()
                    } else {
                        if (App.platform !== "android")
                            pane.win.toast(qsTr("Touch the fingerprint reader"))
                        App.lock.enableBiometric(function (r) {
                            if (!r.ok)
                                bioRow.problem = r.error
                        })
                    }
                    checked = Qt.binding(() => App.lock.biometricEnabled)
                }
            }
        }
        SettingRow {
            visible: App.lock.enabled
            title: qsTr("Lock after")
            subtitle: qsTr("Of no activity, or of being in the background.")
            Segmented {
                value: App.prefs.lockTimeoutMinutes
                options: [
                    { value: 1, label: qsTr("1 min") }, { value: 5, label: qsTr("5 min") },
                    { value: 15, label: qsTr("15 min") }, { value: 60, label: qsTr("1 h") },
                    { value: 0, label: qsTr("Never") }
                ]
                onChosen: value => App.prefs.lockTimeoutMinutes = value
            }
        }
        SettingRow {
            visible: App.lock.enabled
            iconName: "key"
            readonly property bool held: App.prefs.meshService || App.prefs.meshBackground
            title: held ? qsTr("Tokens and certificates are not sealed")
                        : qsTr("Tokens and certificates are sealed with the passcode")
            subtitle: held
                      ? qsTr("The background connection has to read them with nobody there to unlock. Chats saved for offline use are still sealed.")
                      : qsTr("After a restart Talos stays offline until it is unlocked. Attachments it has cached are deleted whenever it locks.")
        }
        SettingRow {
            visible: App.lock.enabled
            title: qsTr("Erase the connection after 10 wrong passcodes")
            subtitle: qsTr("Removes this device's connections and cached chats. Nothing on Talon is touched.")
            TSwitch {
                checked: App.prefs.lockEraseAfterFailures
                onToggled: App.prefs.lockEraseAfterFailures = checked
            }
        }
        SettingRow {
            visible: App.lock.enabled
            clickable: true
            iconName: "lock"
            title: qsTr("Lock now")
            onClicked: {
                pane.win.settingsOpen = false
                App.lock.lock()
            }
        }
    }

    Card {
        title: qsTr("Commands from Talon")

        SettingRow {
            first: true
            title: qsTr("Ask me before each device command")
            subtitle: qsTr("Applies when Talon controls this device (Devices & mesh). A command nobody approves in time is refused.")
            TSwitch {
                checked: App.prefs.lockGateCommands
                onToggled: App.prefs.lockGateCommands = checked
            }
        }
    }

    Card {
        visible: App.platform === "android"
        title: qsTr("Screen privacy")

        SettingRow {
            first: true
            title: qsTr("Hide the app from screenshots and the app switcher")
            TSwitch {
                checked: App.prefs.screenPrivacy
                onToggled: App.prefs.screenPrivacy = checked
            }
        }
    }

    Card {
        title: qsTr("Where secrets are kept")

        SettingRow {
            first: true
            iconName: "key"
            title: qsTr("Tokens, certificates and the lock record live in %1.").arg(App.store.secretBackend)
        }
    }

    Sheet {
        id: lockSheet
        maxWidth: 420
        property string mode
        property string problem

        title: mode === "enable" ? qsTr("Set a passcode")
             : mode === "change" ? qsTr("Change passcode") : qsTr("Turn off app lock")

        function begin(m) {
            mode = m
            problem = ""
            current.text = ""
            next.text = ""
            again.text = ""
            open()
            if (m === "enable")
                next.forceActiveFocus()
            else
                current.forceActiveFocus()
        }
        function finish(r) {
            if (r.ok)
                close()
            else
                problem = r.error
        }
        function submit() {
            problem = ""
            if (mode !== "disable" && next.text !== again.text) {
                problem = qsTr("The two passcodes don't match.")
                return
            }
            if (mode === "enable")
                App.lock.enable(next.text, finish)
            else if (mode === "change")
                App.lock.change(current.text, next.text, finish)
            else
                App.lock.disable(current.text, finish)
        }

        TField {
            id: current
            visible: lockSheet.mode !== "enable"
            Layout.fillWidth: true
            echoMode: TextInput.Password
            placeholderText: qsTr("Current passcode")
            onAccepted: lockSheet.mode === "disable" ? lockSheet.submit() : next.forceActiveFocus()
        }
        TField {
            id: next
            visible: lockSheet.mode !== "disable"
            Layout.fillWidth: true
            echoMode: TextInput.Password
            placeholderText: qsTr("New passcode (6 or more characters)")
            onAccepted: again.forceActiveFocus()
        }
        TField {
            id: again
            visible: lockSheet.mode !== "disable"
            Layout.fillWidth: true
            echoMode: TextInput.Password
            placeholderText: qsTr("Repeat it")
            onAccepted: lockSheet.submit()
        }
        Text {
            visible: lockSheet.problem !== ""
            text: lockSheet.problem
            color: Theme.danger
            font.pixelSize: Theme.small + 1
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Text {
            visible: lockSheet.mode === "enable"
            text: qsTr("If you forget it, the only way back in is to reset the app and pair this device again.")
            color: Theme.textTertiary
            font.pixelSize: Theme.small + 1
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        footer: [
            TButton {
                kind: lockSheet.mode === "disable" ? "danger" : "primary"
                text: lockSheet.mode === "disable" ? qsTr("Turn off") : qsTr("Save")
                busy: App.lock.busy
                enabled: !App.lock.busy
                onClicked: lockSheet.submit()
            },
            TButton { kind: "ghost"; text: qsTr("Cancel"); onClicked: lockSheet.close() }
        ]
    }
}
