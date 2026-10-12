// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import Talos.Native

Pane {
    id: pane

    Card {
        title: qsTr("Language")

        SettingRow {
            first: true
            title: qsTr("Language of the interface")
            subtitle: qsTr("Talon's own replies and messages are not translated.")
            Segmented {
                value: App.prefs.language
                options: App.languages().map(l => ({ value: l.code, label: l.name }))
                onChosen: value => App.prefs.language = value
            }
        }
    }

    Card {
        title: qsTr("Writing")

        SettingRow {
            first: true
            visible: !App.mobile
            title: qsTr("Enter sends the message")
            subtitle: qsTr("Shift+Enter starts a new line. With this off, Ctrl+Enter sends.")
            TSwitch {
                checked: App.prefs.sendOnEnter
                onToggled: App.prefs.sendOnEnter = checked
            }
        }
        SettingRow {
            first: App.mobile
            clickable: true
            iconName: "keyboard"
            title: qsTr("Keyboard shortcuts")
            chevron: true
            onClicked: pane.win.openShortcuts()
        }
    }

    Card {
        title: qsTr("Notifications")

        SettingRow {
            first: true
            title: qsTr("Notify me about replies")
            subtitle: qsTr("When a reply arrives in a chat you aren't looking at, or while the app is in the background.")
            TSwitch {
                checked: App.prefs.notifications
                onToggled: App.prefs.notifications = checked
            }
        }
        SettingRow {
            enabled: App.prefs.notifications
            title: qsTr("Show what the reply says")
            subtitle: qsTr("Off, a notification only says that something arrived. Always hidden while the app is locked.")
            TSwitch {
                checked: App.prefs.notifyPreview
                onToggled: App.prefs.notifyPreview = checked
            }
        }
    }

    Card {
        visible: !App.mobile
        title: qsTr("On this computer")

        SettingRow {
            first: true
            enabled: App.desktop.trayAvailable
            title: qsTr("Keep running in the tray")
            subtitle: App.desktop.trayAvailable
                      ? qsTr("Closing the window hides it; replies still notify and this device stays on the mesh.")
                      : qsTr("This desktop has no system tray.")
            TSwitch {
                checked: App.prefs.closeToTray
                onToggled: App.prefs.closeToTray = checked
            }
        }
        SettingRow {
            visible: !App.mobile
            title: qsTr("Quick-ask shortcut")
            subtitle: !App.shortcut.available
                      ? qsTr("This desktop has no shortcut service for apps. Bind a key to “talos --quick” in its own shortcut settings instead.")
                      : App.shortcut.state === "bound"
                        ? (App.shortcut.trigger !== ""
                           ? qsTr("%1 opens a small window for one question, from anywhere.").arg(App.shortcut.trigger)
                           : qsTr("Bound. The keys are shown, and can be changed, in your desktop's shortcut settings."))
                        : App.shortcut.state === "failed" ? App.shortcut.error
                        : App.shortcut.state === "binding" ? qsTr("Waiting for your desktop…")
                        : qsTr("A system-wide key that opens a small window for one question. Your desktop asks which keys to use.")
            TSwitch {
                enabled: App.shortcut.available
                checked: App.prefs.quickShortcut
                onToggled: App.prefs.quickShortcut = checked
            }
        }
        SettingRow {
            enabled: App.desktop.autostartSupported
            title: qsTr("Open when I sign in")
            subtitle: App.prefs.closeToTray ? qsTr("Starts hidden in the tray.") : ""
            TSwitch {
                checked: App.prefs.autostart
                onToggled: App.prefs.autostart = checked
            }
        }
    }
}
