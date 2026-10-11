// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

Pane {
    id: pane

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
