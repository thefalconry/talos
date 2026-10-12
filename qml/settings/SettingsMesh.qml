// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import Talos.Native

// This device on Talon's mesh, the other devices, and what was run here.
Pane {
    id: pane

    readonly property var mesh: App.mesh
    property var trail: mesh.audit()

    Component.onCompleted: App.session.refreshDevices()
    Connections {
        target: App.mesh
        function onAuditChanged() { pane.trail = App.mesh.audit() }
    }

    Card {
        title: qsTr("This device")
        caption: qsTr("%1 · %2").arg(pane.mesh.deviceName).arg(pane.mesh.state)

        SettingRow {
            first: true
            title: qsTr("Join the mesh")
            subtitle: qsTr("Registers this device with Talon so it can be listed, rung, asked for its status and located.")
            TSwitch {
                checked: App.prefs.meshSharing
                onToggled: App.prefs.meshSharing = checked
            }
        }
        SettingRow {
            visible: App.prefs.meshSharing && App.platform === "android"
            title: qsTr("Stay reachable with the app closed")
            subtitle: qsTr("Keeps the connection up while Talos is in the background, behind a notification that stays visible. It ends when Talos is swiped out of recent apps.")
            TSwitch {
                checked: App.prefs.meshBackground
                onToggled: App.prefs.meshBackground = checked
            }
        }
        SettingRow {
            visible: App.prefs.meshSharing
            enabled: !pane.mesh.sandboxed
            title: qsTr("Let Talon control this device")
            subtitle: pane.mesh.sandboxed
                      ? qsTr("Unavailable in a sandboxed build: commands would only reach the sandbox, not this computer.")
                      : qsTr("Talon may run shell commands and read, write and transfer files here, as you. Granted for this server only.")
            TSwitch {
                checked: pane.mesh.deviceControl
                onToggled: {
                    if (!checked) {
                        pane.mesh.deviceControl = false
                        return
                    }
                    checked = false
                    pane.win.confirm(
                        qsTr("Give Talon control of this device?"),
                        qsTr("Talon, and anyone able to instruct it, can then run any command and touch any file your account can. Only do this for a Talon you run yourself."),
                        qsTr("Allow"), () => pane.mesh.deviceControl = true)
                }
            }
        }
        SettingRow {
            visible: App.prefs.meshSharing && pane.mesh.deviceControl
            title: qsTr("Ask me before each command")
            subtitle: qsTr("Every shell, file and transfer command waits for approval in this app, and is refused when nobody answers.")
            TSwitch {
                checked: App.prefs.lockGateCommands
                onToggled: App.prefs.lockGateCommands = checked
            }
        }
        SettingRow {
            visible: App.prefs.meshSharing && pane.mesh.deviceControl
                     && pane.mesh.screenControlAvailable
            title: qsTr("Let Talon see and operate the screen")
            subtitle: qsTr("Screenshots, pointer and keyboard, through the desktop's own remote-control permission. The desktop asks you the first time and shows when a session is running.")
                      + (App.screen.granted ? "\n" + qsTr("A session is active.") : "")
            TButton {
                visible: App.prefs.meshScreenControl
                compact: true
                text: qsTr("Revoke")
                onClicked: {
                    App.screen.revoke()
                    App.prefs.meshScreenControl = false
                }
            }
            TSwitch {
                checked: App.prefs.meshScreenControl
                onToggled: {
                    if (!checked) {
                        App.prefs.meshScreenControl = false
                        return
                    }
                    checked = false
                    pane.win.confirm(
                        qsTr("Let Talon operate this screen?"),
                        qsTr("Talon, and anyone able to instruct it, could then see everything on this screen and click and type as you. Your desktop will also ask you to allow it."),
                        qsTr("Allow"), () => App.prefs.meshScreenControl = true)
                }
            }
        }
        SettingRow {
            visible: App.prefs.meshSharing && pane.mesh.deviceControl
            iconName: "download"
            title: pane.mesh.canUpdateSelf ? qsTr("Talon can update this app")
                                           : qsTr("Talon can't update this app")
            subtitle: pane.mesh.canUpdateSelf
                      ? qsTr("A pushed build is installed only if it matches the SHA-256 Talon names and is a Talos build no older than this one.")
                      : qsTr("This copy is installed by the system (or sandboxed), so it is updated through its package manager.")
        }
        SettingRow {
            visible: App.prefs.meshSharing
            enabled: pane.mesh.locationAvailable
            title: qsTr("Report location on a schedule")
            subtitle: pane.mesh.locationAvailable
                      ? qsTr("Without this, the location is only sent when Talon asks for it.")
                      : qsTr("This device has no location service.")
            TSwitch {
                checked: App.prefs.meshPeriodic
                onToggled: App.prefs.meshPeriodic = checked
            }
        }
        SettingRow {
            visible: App.prefs.meshSharing && App.prefs.meshPeriodic && pane.mesh.locationAvailable
            title: qsTr("Every")
            Segmented {
                value: App.prefs.meshIntervalSeconds
                options: [
                    { value: 60, label: qsTr("1 min") }, { value: 300, label: qsTr("5 min") },
                    { value: 900, label: qsTr("15 min") }, { value: 3600, label: qsTr("1 h") }
                ]
                onChosen: value => App.prefs.meshIntervalSeconds = value
            }
        }
        SettingRow {
            visible: App.prefs.meshSharing && pane.mesh.locationAvailable
            clickable: true
            iconName: "pin"
            title: qsTr("Send location now")
            onClicked: pane.mesh.reportLocationNow()
        }
    }

    Card {
        visible: App.serviceSupported
        title: qsTr("In the background")
        caption: qsTr("Keeps this device on the mesh when the app is closed, as a service of your own user account.")

        SettingRow {
            id: serviceRow
            property string problem
            first: true
            enabled: App.prefs.meshSharing || App.prefs.meshService
            title: qsTr("Stay on the mesh without the app open")
            subtitle: (App.prefs.meshService
                       ? qsTr("Service: %1.").arg(App.serviceState)
                         + (App.serviceLingering ? "" : " " + qsTr("It stops when you sign out; run `loginctl enable-linger` to keep it up across sign-outs and reboots."))
                         + " " + qsTr("While it is installed, credentials are kept in a private file rather than the keyring, because the service runs without your desktop session.")
                       : qsTr("Installs a background service. Screen control and the ring sound still need the app."))
                      + (problem !== "" ? "\n" + problem : "")
            TSwitch {
                checked: App.prefs.meshService
                onToggled: {
                    serviceRow.problem = App.setBackgroundService(checked)
                    checked = Qt.binding(() => App.prefs.meshService)
                }
            }
        }
    }

    Card {
        visible: App.prefs.meshSharing && pane.mesh.deviceControl
        title: qsTr("Limits")
        caption: qsTr("Bounds on what a burst of commands can pile onto this device.")

        SettingRow {
            first: true
            title: qsTr("Commands at once")
            Segmented {
                value: App.prefs.meshMaxConcurrent
                options: [{ value: 1, label: "1" }, { value: 2, label: "2" }, { value: 4, label: "4" }, { value: 8, label: "8" }]
                onChosen: value => App.prefs.meshMaxConcurrent = value
            }
        }
        SettingRow {
            title: qsTr("Waiting in the queue")
            Segmented {
                value: App.prefs.meshMaxQueued
                options: [{ value: 0, label: "0" }, { value: 8, label: "8" }, { value: 16, label: "16" }, { value: 64, label: "64" }]
                onChosen: value => App.prefs.meshMaxQueued = value
            }
        }
        SettingRow {
            title: qsTr("Largest file Talon may write")
            Segmented {
                value: App.prefs.meshMaxWriteGiB
                options: [{ value: 1, label: "1 GiB" }, { value: 4, label: "4 GiB" }, { value: 16, label: "16 GiB" }, { value: 64, label: "64 GiB" }]
                onChosen: value => App.prefs.meshMaxWriteGiB = value
            }
        }
    }

    Card {
        title: qsTr("Devices")

        Repeater {
            model: App.session.devices

            SettingRow {
                id: deviceRow
                required property var modelData
                required property int index
                first: index === 0
                iconName: modelData.platform === "android" || modelData.platform === "ios" ? "phone" : "monitor"
                title: modelData.name + (modelData.self ? "  (" + qsTr("this device") + ")" : "")
                subtitle: (modelData.online ? qsTr("Online") : qsTr("Last seen %1").arg(App.relativeTime(modelData.lastSeen)))
                          + " · " + modelData.platform + " · " + modelData.appVersion
                          + (modelData.battery !== undefined
                             ? " · " + qsTr("battery %1%").arg(modelData.battery) + (modelData.charging ? " ⚡" : "") : "")
                          + (modelData.hasLocation
                             ? "\n" + qsTr("Located %1").arg(App.relativeTime(modelData.location.ts)) : "")

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: deviceRow.modelData.online ? Theme.success : Theme.borderStrong
                }
                TButton {
                    visible: deviceRow.modelData.hasLocation === true
                    compact: true
                    iconName: "pin"
                    text: qsTr("Map")
                    onClicked: pane.win.openLink(App.mapLink(deviceRow.modelData.location.lat,
                                                             deviceRow.modelData.location.lon))
                }
            }
        }
        SettingRow {
            first: App.session.devices.length === 0
            clickable: true
            iconName: "refresh"
            title: App.session.devices.length === 0 ? qsTr("No devices have registered") : qsTr("Refresh")
            onClicked: App.session.refreshDevices()
        }
    }

    Card {
        visible: pane.trail.length > 0
        title: qsTr("Run on this device")
        caption: qsTr("Every mesh command answered here, newest first. Kept on this device only.")

        Repeater {
            model: Math.min(pane.trail.length, 60)

            SettingRow {
                required property int index
                readonly property var entry: pane.trail[index] || ({})
                first: index === 0
                iconName: entry.ok ? "check" : "alert"
                danger: entry.ok === false
                title: entry.name + (entry.detail ? "  " + entry.detail : "")
                subtitle: App.dateTimeLabel(entry.ts) + " · " + entry.elapsedMs + " ms"
                          + (entry.message ? " · " + entry.message : "")
            }
        }
        SettingRow {
            clickable: true
            iconName: "trash"
            title: qsTr("Clear this list")
            onClicked: pane.mesh.clearAudit()
        }
    }
}
