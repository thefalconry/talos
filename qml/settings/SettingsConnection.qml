// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

Pane {
    id: pane

    readonly property var endpoint: App.session.endpoint
    property var identity: ({})

    function refreshIdentity() {
        if (App.session.conn === Session.Connected)
            App.session.whoami(result => pane.identity = result)
        else
            identity = ({})
    }
    Component.onCompleted: refreshIdentity()
    Connections {
        target: App.session
        function onConnChanged() { pane.refreshIdentity() }
        function onEndpointChanged() { pane.refreshIdentity() }
    }

    Card {
        title: qsTr("Current connection")

        SettingRow {
            first: true
            iconName: App.session.conn === Session.Connected ? "check"
                    : App.session.connFatal ? "alert" : "offline"
            title: App.session.conn === Session.Connected
                   ? qsTr("Connected to %1").arg(App.session.botName)
                   : App.session.conn === Session.Connecting
                     ? (App.session.connNote || qsTr("Connecting…"))
                     : App.session.conn === Session.Error ? qsTr("Not connected") : qsTr("Idle")
            subtitle: App.session.conn === Session.Error
                      ? App.session.connError
                      : (pane.endpoint.address || "")
                        + (pane.endpoint.fallback
                           ? " · " + qsTr("a fallback address, while the saved one isn't answering")
                           : pane.endpoint.fallbacks > 0
                             ? " · " + qsTr("%n fallback address(es) known", "", pane.endpoint.fallbacks)
                             : "")

            TButton {
                compact: true
                text: App.session.conn === Session.Connected ? qsTr("Reconnect") : qsTr("Retry")
                onClicked: App.session.start()
            }
        }
        SettingRow {
            visible: !pane.endpoint.local
            iconName: pane.endpoint.tls ? "lock" : "alert"
            title: pane.endpoint.tls ? qsTr("Encrypted") : qsTr("Not encrypted")
            subtitle: !pane.endpoint.tls
                      ? qsTr("Fine on a private network; anywhere else the token travels in the clear.")
                      : pane.endpoint.fingerprint === ""
                        ? qsTr("The server's certificate is vouched for by an authority this device trusts.")
                        : (pane.endpoint.pinEnforced
                           ? qsTr("Pinned to this certificate; any other is refused:\n%1")
                           : qsTr("Pin from the pairing link, enforced once the server shows this certificate:\n%1"))
                          .arg(pane.endpoint.fingerprint)
        }
        SettingRow {
            visible: pane.endpoint.hasClientCert === true
            iconName: "shield"
            title: qsTr("Client certificate")
            subtitle: qsTr("Presented on every connection (mutual TLS), including downloads of links that point at this server.")
        }
        SettingRow {
            visible: App.session.conn === Session.Connected && (pane.identity.kind || "") !== ""
            iconName: "key"
            title: pane.identity.kind === "device" ? qsTr("This device's own credential")
                 : pane.identity.kind === "shared" ? qsTr("Shared bridge token")
                 : qsTr("Open bridge (no token)")
            subtitle: qsTr("Allowed: %1").arg((pane.identity.scopes || []).join(", "))
                      + (pane.identity.kind === "device"
                         ? "\n" + qsTr("Issued to this device alone; Talon can revoke it without touching other devices.")
                         : "")
        }
    }

    Card {
        title: qsTr("Connections")
        caption: qsTr("Credentials are kept in %1, never in the settings file.").arg(App.store.secretBackend)

        Repeater {
            model: App.store.profiles

            SettingRow {
                id: profileRow
                required property var modelData
                required property int index
                first: index === 0
                iconName: modelData.local ? "monitor" : "server"
                title: modelData.name
                subtitle: modelData.address
                          + (modelData.hasClientCert ? " · " + qsTr("client certificate") : "")
                          + (modelData.active ? " · " + qsTr("in use") : "")

                TButton {
                    visible: !profileRow.modelData.active
                    compact: true
                    text: qsTr("Use")
                    onClicked: App.session.switchProfile(profileRow.modelData.id)
                }
                IconButton {
                    iconName: "edit"
                    tip: qsTr("Edit")
                    onClicked: editor.edit(App.profileForm(profileRow.modelData.id))
                }
                IconButton {
                    visible: !profileRow.modelData.active
                    iconName: "trash"
                    tip: qsTr("Remove")
                    onClicked: pane.win.confirm(
                        qsTr("Remove this connection?"),
                        qsTr("“%1” and its saved credentials are deleted from this device.").arg(profileRow.modelData.name),
                        qsTr("Remove"), () => App.store.removeProfile(profileRow.modelData.id))
                }
            }
        }
        SettingRow {
            first: App.store.profiles.length === 0
            clickable: true
            iconName: "plus"
            title: qsTr("Add a connection")
            onClicked: editor.edit(App.newProfileForm())
        }
        SettingRow {
            clickable: true
            iconName: "link"
            title: qsTr("Pair with a link")
            subtitle: qsTr("Uses the link on your clipboard: the address /mesh link gives you, or a talon://pair link.")
            onClicked: pane.win.pairSheet.offer(App.clipboardText())
        }
    }

    Card {
        title: qsTr("Start over")

        SettingRow {
            first: true
            clickable: true
            danger: true
            iconName: "trash"
            title: qsTr("Forget every connection")
            subtitle: qsTr("Removes all connections, their credentials and everything cached from them on this device.")
            onClicked: pane.win.confirm(
                qsTr("Forget every connection?"),
                qsTr("Tokens, certificates and cached chats are erased from this device. Nothing on Talon changes."),
                qsTr("Erase"), () => {
                    pane.win.settingsOpen = false
                    App.forgetEverything()
                })
        }
    }

    Sheet {
        id: editor
        title: qsTr("Connection")
        maxWidth: 520

        function edit(values) {
            form.load(values)
            open()
        }

        ConnectionForm {
            id: form
            Layout.fillWidth: true
            Layout.bottomMargin: 4
            submitLabel: qsTr("Save and connect")
            onSubmitted: editor.close()
        }
    }
}
