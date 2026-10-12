// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import Talos.Native

// The connection profile editor, used for first run and in Settings.
//
// A hand-typed HTTPS server is never sent the token on trust alone: the
// form first looks at the certificate the server presents (sending no
// credential) and asks the person to confirm its fingerprint.
ColumnLayout {
    id: form

    /// The working copy; see App.newProfileForm() for its fields.
    property var values: App.newProfileForm()
    property bool busy: false
    property string error
    property string notice
    property string submitLabel: qsTr("Connect")
    property bool advanced: false
    signal submitted()

    readonly property var win: C.ApplicationWindow.window
    readonly property bool remote: values.mode === "remote"

    spacing: 14

    function load(next) {
        values = next
        error = ""
        notice = ""
        advanced = next.fingerprint !== "" || next.hasClientCert || next.localUrl !== ""
        address.text = next.address
        token.text = next.token || ""
        fingerprint.text = next.fingerprint
        localUrl.text = next.localUrl
        nameField.text = next.name
        launch.text = next.launchCommand
        certPassword.text = ""
    }

    function set(key, value) {
        const next = Object.assign({}, values)
        next[key] = value
        values = next
    }

    function collect() {
        const next = Object.assign({}, values)
        next.address = address.text
        next.token = token.text
        next.fingerprint = fingerprint.text
        next.localUrl = localUrl.text
        next.name = nameField.text
        next.launchCommand = launch.text
        values = next
        return next
    }

    function submit() {
        error = ""
        notice = ""
        const next = collect()
        const check = App.checkForm(next)
        if (!check.ok) {
            error = check.error
            return
        }
        if (!check.needsFingerprint) {
            commit(next)
            return
        }
        busy = true
        App.probeForm(next, function (probe) {
            form.busy = false
            if (!probe.reached) {
                form.error = qsTr("Nothing answered at %1. Check the address, and that Talon's bridge is reachable from here.").arg(check.address)
                return
            }
            // A certificate this device already trusts needs no pin.
            if (probe.trusted || probe.fingerprint === "") {
                form.commit(next)
                return
            }
            trustSheet.candidate = probe
            trustSheet.open()
        })
    }

    function commit(next) {
        App.connectWithForm(next)
        submitted()
    }

    Component.onCompleted: load(values)

    // ── Where Talon runs ────────────────────────────────────────────────────
    Segmented {
        visible: !App.mobile
        Layout.alignment: Qt.AlignHCenter
        value: form.values.mode
        options: [
            { value: "local", label: qsTr("This computer") },
            { value: "remote", label: qsTr("Another machine") }
        ]
        onChosen: value => form.set("mode", value)
    }

    ColumnLayout {
        visible: !form.remote
        Layout.fillWidth: true
        spacing: 12

        Text {
            text: qsTr("Talon running on this computer is found automatically: the daemon writes its bridge address and token to its own data folder, and this app reads them from there. Nothing to type.")
            color: Theme.textSecondary
            font.pixelSize: Theme.body
            wrapMode: Text.Wrap
            lineHeight: 1.3
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: qsTr("Start Talon when it isn't running")
                    color: Theme.text
                    font.pixelSize: Theme.body
                }
                Text {
                    text: qsTr("Runs the command below if no bridge is found.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.small + 1
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
            TSwitch {
                checked: form.values.manageDaemon === true
                onToggled: form.set("manageDaemon", checked)
            }
        }
        TField {
            id: launch
            visible: form.values.manageDaemon === true
            Layout.fillWidth: true
            font.family: Theme.mono
            placeholderText: "talon start"
        }
    }

    // ── A bridge somewhere else ─────────────────────────────────────────────
    ColumnLayout {
        visible: form.remote
        Layout.fillWidth: true
        spacing: 12

        Field {
            label: qsTr("Server")
            hint: qsTr("An address or a full URL. A bare host means Talon's own port, 19880.")
            TField {
                id: address
                Layout.fillWidth: true
                placeholderText: "talon.example.com  ·  192.168.1.20:19880"
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                onAccepted: form.submit()
            }
        }
        RowLayout {
            // A scheme typed into the address already decides this.
            visible: !/^[a-z][a-z0-9+.-]*:\/\//i.test(address.text)
            Layout.fillWidth: true

            Text {
                text: qsTr("Encrypted connection (HTTPS)")
                color: Theme.text
                font.pixelSize: Theme.body
                Layout.fillWidth: true
            }
            TSwitch {
                checked: form.values.tls === true
                onToggled: form.set("tls", checked)
            }
        }
        Field {
            label: qsTr("Token")
            hint: form.values.hasToken
                  ? qsTr("A token is saved. Leave this empty to keep it.")
                  : qsTr("The bridge token from Talon's configuration, or leave empty for an open bridge.")
            TField {
                id: token
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: form.values.hasToken ? qsTr("Unchanged") : qsTr("Bridge token")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData
                onAccepted: form.submit()
            }
        }

        TButton {
            kind: "ghost"
            compact: true
            iconName: form.advanced ? "chevron-up" : "chevron-down"
            text: qsTr("Client certificate, pinning and more")
            onClicked: form.advanced = !form.advanced
        }

        ColumnLayout {
            visible: form.advanced
            Layout.fillWidth: true
            spacing: 12

            // Mutual TLS: the certificate a reverse proxy demands.
            Field {
                label: qsTr("Client certificate (mutual TLS)")
                hint: qsTr("For a server behind a reverse proxy that only lets in devices holding a certificate. It is presented on every connection; Talon's token is still needed.")

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Icon {
                        name: form.values.hasClientCert ? "shield" : "key"
                        size: 17
                        color: form.values.hasClientCert ? Theme.success : Theme.textTertiary
                    }
                    Text {
                        text: form.values.clientP12 !== "" ? qsTr("%1 (ready to save)").arg(form.values.certName)
                            : form.values.hasClientCert && !form.values.clearClientCert ? qsTr("A certificate is installed")
                            : qsTr("None")
                        color: Theme.text
                        font.pixelSize: Theme.body
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    TButton {
                        compact: true
                        visible: form.values.hasClientCert && !form.values.clearClientCert
                        text: qsTr("Remove")
                        onClicked: {
                            const next = Object.assign({}, form.values)
                            next.clearClientCert = true
                            next.clientP12 = ""
                            next.hasClientCert = false
                            form.values = next
                        }
                    }
                    TButton {
                        compact: true
                        iconName: "upload"
                        text: qsTr("Import .p12")
                        onClicked: certPicker.open()
                    }
                }
                RowLayout {
                    visible: certPicker.chosen.toString() !== "" && form.values.clientP12 === ""
                    Layout.fillWidth: true
                    spacing: 8

                    TField {
                        id: certPassword
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: qsTr("Password for %1").arg(App.fileName(certPicker.chosen))
                        onAccepted: importButton.clicked()
                    }
                    TButton {
                        id: importButton
                        kind: "primary"
                        compact: true
                        text: qsTr("Open")
                        onClicked: {
                            const r = App.importCertificate(certPicker.chosen, certPassword.text)
                            if (!r.ok) {
                                form.error = r.error
                                return
                            }
                            form.error = ""
                            const next = Object.assign({}, form.values)
                            next.clientP12 = r.p12
                            next.certPassword = certPassword.text
                            next.certName = r.name
                            next.hasClientCert = true
                            next.clearClientCert = false
                            form.values = next
                            certPassword.text = ""
                        }
                    }
                }
            }
            Field {
                label: qsTr("Pinned certificate fingerprint")
                hint: qsTr("SHA-256 of the bridge's own certificate. Leave empty and it is shown for you to confirm on the first connection. Clear it after reinstalling Talon.")
                TField {
                    id: fingerprint
                    Layout.fillWidth: true
                    font.family: Theme.mono
                    font.pixelSize: Theme.small
                    placeholderText: "AA:BB:CC:…"
                }
            }
            Field {
                label: qsTr("Local network address")
                hint: qsTr("Optional. Used instead of the server address whenever it answers, which skips the proxy at home.")
                TField {
                    id: localUrl
                    Layout.fillWidth: true
                    placeholderText: "192.168.1.20:19880"
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                }
            }
            Field {
                label: qsTr("Name")
                TField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Shown in the app, e.g. Home server")
                }
            }
        }
    }

    Text {
        visible: form.error !== ""
        text: form.error
        color: Theme.danger
        font.pixelSize: Theme.small + 1
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    Text {
        visible: {
            const check = form.remote ? App.checkForm(Object.assign({}, form.values, { address: address.text })) : ({})
            return (check.warning || "") !== ""
        }
        text: qsTr("This address isn't on a private network and the connection isn't encrypted: the token would travel in the clear.")
        color: Theme.warning
        font.pixelSize: Theme.small + 1
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        TButton {
            visible: form.remote
            kind: "ghost"
            iconName: "link"
            text: qsTr("Paste pairing link")
            onClicked: form.win.pairSheet.offer(App.clipboardText())
        }
        Item { Layout.fillWidth: true }
        TButton {
            kind: "primary"
            text: form.submitLabel
            busy: form.busy
            enabled: !form.busy
            onClicked: form.submit()
        }
    }

    FilePicker {
        id: certPicker
        property url chosen
        title: qsTr("Choose a client certificate")
        nameFilters: [qsTr("Certificates (*.p12 *.pfx)"), qsTr("All files (*)")]
        onAccepted: {
            chosen = selectedFile
            form.set("clientP12", "")
            certPassword.forceActiveFocus()
        }
    }

    // First contact with a self-signed bridge: confirm what it presented.
    Sheet {
        id: trustSheet
        title: qsTr("Check this server's certificate")
        maxWidth: 500
        property var candidate: ({})

        Text {
            text: qsTr("%1 presented a certificate this device doesn't know yet. That is normal for Talon's own certificate. Compare the fingerprint with the one Talon prints at startup (or `talon status`) before trusting it.").arg(trustSheet.candidate.address || "")
            color: Theme.textSecondary
            font.pixelSize: Theme.body
            wrapMode: Text.Wrap
            lineHeight: 1.25
            Layout.fillWidth: true
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: prettyFingerprint.implicitHeight + 24
            radius: Theme.radius
            color: Theme.sunken

            TextEdit {
                id: prettyFingerprint
                anchors.fill: parent
                anchors.margins: 12
                text: trustSheet.candidate.pretty || ""
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                color: Theme.text
                font.family: Theme.mono
                font.pixelSize: Theme.small + 1
            }
        }
        Text {
            text: qsTr("From then on the app refuses any other certificate at this address.")
            color: Theme.textTertiary
            font.pixelSize: Theme.small + 1
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        footer: [
            TButton {
                kind: "primary"
                text: qsTr("Trust and connect")
                onClicked: {
                    fingerprint.text = trustSheet.candidate.pretty
                    trustSheet.close()
                    form.commit(form.collect())
                }
            },
            TButton { kind: "ghost"; text: qsTr("Cancel"); onClicked: trustSheet.close() }
        ]
    }

    component Field: ColumnLayout {
        property string label
        property string hint
        default property alias control: holder.data

        Layout.fillWidth: true
        spacing: 5

        Text {
            text: parent.label
            color: Theme.text
            font.pixelSize: Theme.small + 1
            font.weight: Font.Medium
        }
        ColumnLayout {
            id: holder
            Layout.fillWidth: true
            spacing: 8
        }
        Text {
            visible: parent.hint !== ""
            text: parent.hint
            color: Theme.textTertiary
            font.pixelSize: Theme.small
            wrapMode: Text.Wrap
            lineHeight: 1.2
            Layout.fillWidth: true
        }
    }
}
