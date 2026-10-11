// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import Talos.Native

// Covers everything until the passcode is entered: the window and any
// dialog that happened to be open, which is why it is itself the topmost
// popup. The connection keeps running underneath; only the view and the
// cached chats are locked.
C.Popup {
    id: screen

    property string problem
    readonly property int wait: App.lock.retryInSeconds

    parent: C.Overlay.overlay
    z: 10000
    modal: true
    focus: true
    padding: 0
    width: parent.width
    height: parent.height
    closePolicy: C.Popup.NoAutoClose
    visible: App.lock.locked
    enter: null
    exit: null

    background: Rectangle { color: Theme.bg }
    C.Overlay.modal: Rectangle { color: Theme.bg }
    onOpened: {
        problem = ""
        field.forceActiveFocus()
        // Offer the quick way first; the passcode field stays ready.
        if (App.lock.biometricEnabled)
            biometric()
    }
    onClosed: App.lock.cancelBiometric()

    property bool scanning: false
    function biometric() {
        if (scanning || !App.lock.locked)
            return
        scanning = true
        App.lock.unlockWithBiometric(function (r) {
            screen.scanning = false
            if (!r.ok && r.error !== "Cancelled")
                screen.problem = r.error
        })
    }

    function attempt() {
        if (field.text === "" || App.lock.busy || wait > 0)
            return
        problem = ""
        App.lock.unlock(field.text, function (r) {
            field.text = ""
            if (!r.ok) {
                screen.problem = r.error
                field.forceActiveFocus()
            }
        })
    }

    contentItem: Item {
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 340)
        spacing: 16

        BrandMark { size: 52; Layout.alignment: Qt.AlignHCenter }
        Text {
            text: qsTr("Talos is locked")
            color: Theme.text
            font.family: Theme.serif
            font.pixelSize: Theme.fs(26)
            Layout.alignment: Qt.AlignHCenter
        }
        TField {
            id: field
            Layout.fillWidth: true
            echoMode: TextInput.Password
            placeholderText: qsTr("Passcode")
            enabled: !App.lock.busy && screen.wait === 0
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            invalid: screen.problem !== ""
            onAccepted: screen.attempt()
        }
        Text {
            visible: text !== ""
            text: screen.wait > 0 ? qsTr("Too many attempts. Try again in %1 s.").arg(screen.wait)
                                  : screen.problem
            color: Theme.danger
            font.pixelSize: Theme.small + 1
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }
        TButton {
            Layout.fillWidth: true
            kind: "primary"
            text: qsTr("Unlock")
            busy: App.lock.busy
            enabled: field.text !== "" && !App.lock.busy && screen.wait === 0
            onClicked: screen.attempt()
        }
        TButton {
            visible: App.lock.biometricEnabled
            Layout.fillWidth: true
            iconName: "key"
            text: screen.scanning ? qsTr("Waiting for %1…").arg(App.lock.biometricName.toLowerCase())
                                  : qsTr("Use %1").arg(App.lock.biometricName.toLowerCase())
            enabled: !screen.scanning && !App.lock.busy
            onClicked: screen.biometric()
        }
        TButton {
            Layout.alignment: Qt.AlignHCenter
            kind: "ghost"
            compact: true
            text: qsTr("Forgot passcode?")
            onClicked: forgot.open()
        }
    }
    }

    Sheet {
        id: forgot
        title: qsTr("Reset the app lock?")
        maxWidth: 420
        z: 10001

        Text {
            text: qsTr("The passcode can't be recovered. Resetting removes the lock together with this device's connection to Talon and everything cached from it. Your chats stay on Talon; you pair this device again afterwards.")
            color: Theme.textSecondary
            font.pixelSize: Theme.body
            wrapMode: Text.Wrap
            lineHeight: 1.25
            Layout.fillWidth: true
        }
        footer: [
            TButton {
                kind: "danger"
                text: qsTr("Reset and erase")
                onClicked: {
                    forgot.close()
                    App.lock.reset()
                    App.forgetEverything()
                }
            },
            TButton { kind: "ghost"; text: qsTr("Cancel"); onClicked: forgot.close() }
        ]
    }
}
