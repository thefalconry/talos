// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

// What happens to links that are not ordinary web links: pairing links,
// the bridge's secret-drop form, and files or pages the bridge serves.
// Bridge links are fetched over the app's own connection, which carries the
// client certificate a browser would not have.
Item {
    id: root

    property alias pair: pairSheet
    property alias secret: secretSheet
    property alias downloads: downloadSheet

    // ── talon://pair ────────────────────────────────────────────────────────
    Sheet {
        id: pairSheet
        title: qsTr("Pair with this server?")
        maxWidth: 480
        property var form: ({})
        property string problem

        function offer(text) {
            const result = App.formFromPairLink(text || "")
            problem = result.error || ""
            form = result.error ? ({}) : result
            open()
        }

        Text {
            visible: pairSheet.problem !== ""
            text: pairSheet.problem
            color: Theme.text
            font.pixelSize: Theme.body
            wrapMode: Text.Wrap
            lineHeight: 1.25
            Layout.fillWidth: true
        }
        ColumnLayout {
            visible: pairSheet.problem === ""
            Layout.fillWidth: true
            spacing: 10

            Text {
                // Shown exactly as it will be dialled.
                text: pairSheet.form.address || ""
                color: Theme.text
                font.pixelSize: Theme.title
                font.family: Theme.mono
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
            }
            Text {
                text: (pairSheet.form.fingerprint || "") !== ""
                      ? qsTr("The link names the server's certificate; the app will accept no other.")
                      : qsTr("This is an unencrypted connection on a private network.")
                color: Theme.textSecondary
                font.pixelSize: Theme.body
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Text {
                visible: App.store.hasProfile
                text: qsTr("It is added as a new connection and becomes the active one. Your current connection is kept.")
                color: Theme.textTertiary
                font.pixelSize: Theme.small + 1
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Text {
                text: qsTr("Only pair with a link you asked Talon for yourself.")
                color: Theme.textTertiary
                font.pixelSize: Theme.small + 1
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        footer: [
            TButton {
                visible: pairSheet.problem === ""
                kind: "primary"
                text: qsTr("Pair")
                onClicked: {
                    App.connectWithForm(pairSheet.form)
                    pairSheet.close()
                }
            },
            TButton {
                kind: "ghost"
                text: pairSheet.problem === "" ? qsTr("Cancel") : qsTr("Close")
                onClicked: pairSheet.close()
            }
        ]
    }

    // ── /secret?grant=… ─────────────────────────────────────────────────────
    Sheet {
        id: secretSheet
        title: qsTr("Give Talon a secret")
        maxWidth: 480
        property string link
        property string heading
        property string problem
        property bool checking: false
        property bool sending: false
        property bool stored: false

        function offer(l) {
            link = l
            heading = ""
            problem = ""
            stored = false
            checking = true
            value.text = ""
            open()
            App.links.checkSecretDrop(l, function (r) {
                secretSheet.checking = false
                secretSheet.heading = r.heading || ""
                if (!r.live)
                    secretSheet.problem = r.error || qsTr("That link is no longer valid.")
                else
                    value.forceActiveFocus()
            })
        }
        function submit() {
            if (value.text === "" || sending)
                return
            sending = true
            App.links.submitSecretDrop(link, value.text, function (r) {
                secretSheet.sending = false
                value.text = ""
                if (r.ok)
                    secretSheet.stored = true
                else
                    secretSheet.problem = r.error || qsTr("Talon didn't store it. Ask for a new link.")
            })
        }

        Spinner {
            visible: secretSheet.checking
            Layout.alignment: Qt.AlignHCenter
            Layout.margins: 16
        }
        Text {
            visible: !secretSheet.checking
            text: secretSheet.stored ? qsTr("Stored. Talon confirms it in the chat.")
                : secretSheet.problem !== "" ? secretSheet.problem
                : (secretSheet.heading !== "" ? secretSheet.heading + ". " : "")
                  + qsTr("What you type goes straight to Talon's secrets folder over this app's connection. It never passes through the chat or the model, and the link works once.")
            color: secretSheet.problem !== "" && !secretSheet.stored ? Theme.danger : Theme.textSecondary
            font.pixelSize: Theme.body
            wrapMode: Text.Wrap
            lineHeight: 1.25
            Layout.fillWidth: true
        }
        TField {
            id: value
            visible: !secretSheet.checking && !secretSheet.stored && secretSheet.problem === ""
            Layout.fillWidth: true
            echoMode: TextInput.Password
            placeholderText: qsTr("Password, key or token")
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            onAccepted: secretSheet.submit()
        }
        footer: [
            TButton {
                visible: !secretSheet.checking && !secretSheet.stored && secretSheet.problem === ""
                kind: "primary"
                text: qsTr("Store")
                busy: secretSheet.sending
                enabled: value.text !== "" && !secretSheet.sending
                onClicked: secretSheet.submit()
            },
            TButton {
                kind: "ghost"
                text: secretSheet.stored || secretSheet.problem !== "" ? qsTr("Close") : qsTr("Cancel")
                onClicked: secretSheet.close()
            }
        ]
    }

    // ── Files and pages on the bridge ───────────────────────────────────────
    Sheet {
        id: downloadSheet
        title: state === "page" ? qsTr("From Talon") : qsTr("Download")
        maxWidth: state === "page" ? 560 : 440
        dismissable: state !== "busy"
        property string link
        property string state: "busy"   // busy | done | failed | page
        property double fraction: -1
        property string label
        property string file
        property string name
        property string problem
        property string html

        function start(l) {
            link = l
            state = "busy"
            fraction = -1
            label = ""
            problem = ""
            html = ""
            open()
            App.links.download(l, function (r) {
                if (r.cancelled) {
                    downloadSheet.close()
                    return
                }
                if (!r.ok) {
                    downloadSheet.problem = r.error
                    downloadSheet.state = "failed"
                } else if (r.html !== undefined) {
                    // A pairing page carries the link it exists to hand over.
                    if (r.pairLink) {
                        downloadSheet.close()
                        pairSheet.offer(r.pairLink)
                        return
                    }
                    downloadSheet.html = r.html
                    downloadSheet.state = "page"
                } else {
                    downloadSheet.file = r.file
                    downloadSheet.name = r.name
                    downloadSheet.label = r.size
                    downloadSheet.state = "done"
                }
            })
        }

        Connections {
            target: App.links
            function onProgress(link, fraction, label) {
                if (link !== downloadSheet.link)
                    return
                downloadSheet.fraction = fraction
                downloadSheet.label = label
            }
        }

        ColumnLayout {
            visible: downloadSheet.state === "busy"
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: qsTr("Fetching through this app's connection to Talon…")
                color: Theme.textSecondary
                font.pixelSize: Theme.body
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 6
                radius: 3
                color: Theme.sunken
                clip: true

                Rectangle {
                    id: bar
                    height: parent.height
                    radius: 3
                    color: Theme.accent
                    width: downloadSheet.fraction >= 0 ? parent.width * downloadSheet.fraction
                                                       : parent.width * 0.3

                    SequentialAnimation on x {
                        running: downloadSheet.fraction < 0 && downloadSheet.visible
                        loops: Animation.Infinite
                        NumberAnimation { from: -bar.width; to: bar.parent.width; duration: 1100 }
                    }
                }
            }
            Text {
                text: downloadSheet.label
                color: Theme.textTertiary
                font.pixelSize: Theme.small + 1
            }
        }
        Text {
            visible: downloadSheet.state === "done"
            text: qsTr("Saved %1 (%2) to your Downloads folder.").arg(downloadSheet.name).arg(downloadSheet.label)
            color: Theme.text
            font.pixelSize: Theme.body
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Text {
            visible: downloadSheet.state === "failed"
            text: downloadSheet.problem
            color: Theme.danger
            font.pixelSize: Theme.body
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Text {
            visible: downloadSheet.state === "page"
            text: downloadSheet.html
            textFormat: Text.RichText
            color: Theme.text
            font.pixelSize: Theme.body
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            onLinkActivated: link => {
                downloadSheet.close()
                App.links.openExternally(link)
            }
        }
        footer: [
            TButton {
                visible: downloadSheet.state === "done"
                kind: "primary"
                text: qsTr("Open")
                onClicked: {
                    App.links.openFile(downloadSheet.file)
                    downloadSheet.close()
                }
            },
            TButton {
                visible: downloadSheet.state === "done"
                text: qsTr("Show in folder")
                onClicked: {
                    App.links.revealFile(downloadSheet.file)
                    downloadSheet.close()
                }
            },
            TButton {
                kind: "ghost"
                text: downloadSheet.state === "busy" ? qsTr("Cancel") : qsTr("Close")
                onClicked: {
                    if (downloadSheet.state === "busy")
                        App.links.cancel(downloadSheet.link)
                    downloadSheet.close()
                }
            }
        ]
    }
}
