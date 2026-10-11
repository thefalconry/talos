// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

// Talon's own configuration, read from and written back to the daemon.
Pane {
    id: pane

    readonly property var cfg: App.session.daemonConfig
    readonly property bool loaded: cfg.loaded === true
    function can(key) { return loaded && (cfg.editable || []).indexOf(key) >= 0 }
    function save(key, value) {
        const patch = {}
        patch[key] = value
        App.session.updateConfig(patch, null)
    }

    Component.onCompleted: App.session.loadConfig()
    Connections {
        target: App.session
        function onConnChanged() {
            if (App.session.conn === Session.Connected)
                App.session.loadConfig()
        }
    }

    Text {
        visible: !pane.loaded
        text: App.session.conn !== Session.Connected
              ? qsTr("Connect to Talon to see and change its settings.")
              : (pane.cfg.error || qsTr("Loading…"))
        color: Theme.textSecondary
        font.pixelSize: Theme.body
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    Card {
        visible: pane.loaded
        title: qsTr("Status")

        SettingRow {
            first: true
            iconName: pane.cfg.healthy ? "check" : "alert"
            title: pane.cfg.healthy ? qsTr("Healthy") : qsTr("Reporting problems")
            subtitle: qsTr("Up %1 · %2 active sessions · %3 messages handled · %4 MB in use")
                      .arg(pane.cfg.uptime || "").arg(pane.cfg.sessions || 0)
                      .arg(pane.cfg.messagesProcessed || 0).arg(pane.cfg.memoryMb || 0)
        }
        SettingRow {
            iconName: "cpu"
            title: qsTr("Backend: %1").arg(pane.cfg.backend || "")
            subtitle: qsTr("Frontend: %1. Both are chosen in Talon's config file and need a restart to change.").arg(pane.cfg.frontend || "")
        }
    }

    Card {
        visible: pane.loaded
        title: qsTr("General")

        SettingRow {
            first: true
            title: qsTr("Name")
            subtitle: qsTr("What Talon calls itself.")
            TField {
                implicitWidth: 200
                text: pane.cfg.botDisplayName || ""
                enabled: pane.can("botDisplayName")
                onEditingFinished: if (text.trim() !== "" && text !== pane.cfg.botDisplayName)
                                       pane.save("botDisplayName", text.trim())
            }
        }
        SettingRow {
            title: qsTr("Default model")
            subtitle: qsTr("Used by chats that haven't picked their own. Now: %1").arg(pane.cfg.modelDisplay || pane.cfg.model || "")
            TButton {
                compact: true
                text: qsTr("Change")
                enabled: pane.can("model") && App.session.models.length > 0
                onClicked: modelMenu.popup()

                TMenu {
                    id: modelMenu
                    Repeater {
                        model: App.session.models
                        TMenuItem {
                            required property var modelData
                            text: modelData.displayName
                            checkable: true
                            checked: modelData.id === pane.cfg.model
                            onTriggered: pane.save("model", modelData.id)
                        }
                    }
                }
            }
        }
        SettingRow {
            title: qsTr("Timezone")
            subtitle: qsTr("An IANA name such as Europe/Dublin. Empty follows the host.")
            TField {
                implicitWidth: 200
                text: pane.cfg.timezone || ""
                placeholderText: qsTr("Host timezone")
                enabled: pane.can("timezone")
                onEditingFinished: if (text.trim() !== (pane.cfg.timezone || ""))
                                       pane.save("timezone", text.trim())
            }
        }
    }

    Card {
        visible: pane.loaded
        title: qsTr("Background agents")
        caption: qsTr("Work Talon does without being asked.")

        SettingRow {
            first: true
            title: qsTr("Heartbeat")
            subtitle: qsTr("Wakes on a schedule to advance goals and message you when something matters.")
            TSwitch {
                checked: pane.cfg.heartbeat === true
                enabled: pane.can("heartbeat")
                onToggled: pane.save("heartbeat", checked)
            }
        }
        SettingRow {
            visible: pane.cfg.heartbeat === true
            title: qsTr("Heartbeat every")
            Segmented {
                value: pane.cfg.heartbeatIntervalMinutes
                options: [
                    { value: 15, label: qsTr("15 min") }, { value: 30, label: qsTr("30 min") },
                    { value: 60, label: qsTr("1 h") }, { value: 180, label: qsTr("3 h") },
                    { value: 360, label: qsTr("6 h") }
                ]
                onChosen: value => pane.save("heartbeatIntervalMinutes", value)
            }
        }
        SettingRow {
            title: qsTr("Pulse")
            subtitle: qsTr("Periodic check-ins in group chats on the messaging frontends.")
            TSwitch {
                checked: pane.cfg.pulse === true
                enabled: pane.can("pulse")
                onToggled: pane.save("pulse", checked)
            }
        }
        SettingRow {
            visible: pane.cfg.pulse === true
            title: qsTr("Pulse every")
            Segmented {
                value: pane.cfg.pulseIntervalMs
                options: [
                    { value: 300000, label: qsTr("5 min") }, { value: 900000, label: qsTr("15 min") },
                    { value: 1800000, label: qsTr("30 min") }, { value: 3600000, label: qsTr("1 h") }
                ]
                onChosen: value => pane.save("pulseIntervalMs", value)
            }
        }
        SettingRow {
            title: qsTr("Dream")
            subtitle: qsTr("Consolidates memory and writes the diary while idle.")
            TSwitch {
                checked: pane.cfg.dream === true
                enabled: pane.can("dream")
                onToggled: pane.save("dream", checked)
            }
        }
    }

    Card {
        visible: App.session.conn === Session.Connected
        title: qsTr("Actions")

        SettingRow {
            first: true
            clickable: true
            iconName: "spark"
            title: qsTr("Dream now")
            subtitle: qsTr("Consolidate memory and write the diary immediately.")
            onClicked: App.session.control("dream", r => pane.win.toast(r.message))
        }
        SettingRow {
            clickable: true
            danger: true
            iconName: "power"
            title: qsTr("Restart Talon")
            subtitle: qsTr("Applies pending configuration changes. Running turns are interrupted; the app reconnects by itself.")
            onClicked: pane.win.confirm(
                qsTr("Restart Talon?"),
                qsTr("Anything Talon is doing right now is interrupted. It is back in a few seconds."),
                qsTr("Restart"), () => App.session.control("restart", r => pane.win.toast(r.message)))
        }
    }
}
