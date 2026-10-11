// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

Pane {
    id: pane

    readonly property var status: App.session.status

    RowLayout {
        Layout.fillWidth: true
        spacing: 16

        BrandMark { size: 56 }
        ColumnLayout {
            spacing: 2

            Text {
                text: qsTr("Talos")
                color: Theme.text
                font.family: Theme.serif
                font.pixelSize: Theme.fs(24)
            }
            Text {
                text: qsTr("Version %1 · Qt on %2").arg(App.version).arg(App.platform)
                color: Theme.textSecondary
                font.pixelSize: Theme.body
            }
        }
    }

    Card {
        title: qsTr("Connected Talon")

        SettingRow {
            first: true
            title: App.session.conn === Session.Connected ? App.session.botName : qsTr("Not connected")
            subtitle: App.session.conn === Session.Connected
                      ? qsTr("Backend %1 · default model %2 · bridge protocol v%3 · %4 chats")
                        .arg(pane.status.backend || "?").arg(pane.status.model || "?")
                        .arg(pane.status.protocol || 1).arg(pane.status.activeChats || 0)
                      : ""
        }
        SettingRow {
            visible: App.session.conn === Session.Connected
            title: qsTr("Bridge features")
            subtitle: (pane.status.capabilities || []).join(", ") || qsTr("none advertised")
        }
    }

    Card {
        title: qsTr("Help")

        SettingRow {
            first: true
            clickable: true
            chevron: true
            iconName: "keyboard"
            title: qsTr("Keyboard shortcuts")
            onClicked: pane.win.openShortcuts()
        }
        SettingRow {
            clickable: true
            chevron: true
            iconName: "list"
            title: qsTr("This app's log")
            subtitle: qsTr("What the client itself did: connections, retries, mesh commands.")
            onClicked: pane.win.settingsSection = "logs"
        }
        SettingRow {
            clickable: true
            iconName: "copy"
            title: qsTr("Copy diagnostics")
            subtitle: qsTr("Version, platform, connection state and this app's log, with credentials removed.")
            onClicked: {
                App.copy("Talos " + App.version + " on " + App.platform + "\n"
                         + "connection: " + App.session.conn + " " + App.session.connError + "\n"
                         + "endpoint: " + (App.session.endpoint.address || "") + " tls=" + App.session.endpoint.tls + "\n"
                         + "secrets: " + App.store.secretBackend + "\n\n" + App.log.text())
                pane.win.toast(qsTr("Diagnostics copied"))
            }
        }
        SettingRow {
            clickable: true
            iconName: "external"
            title: qsTr("Talon on GitHub")
            onClicked: pane.win.openLink("https://github.com/thefalconry/talon")
        }
    }
}
