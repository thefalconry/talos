// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

Pane {
    id: pane

    readonly property var up: App.updater

    Card {
        title: qsTr("This app")

        SettingRow {
            first: true
            iconName: pane.up.available ? "download" : "check"
            title: pane.up.state === "available" ? qsTr("Version %1 is available").arg(pane.up.latestVersion)
                 : pane.up.state === "ready" ? qsTr("Version %1 is downloaded").arg(pane.up.latestVersion)
                 : pane.up.state === "downloading" ? qsTr("Downloading %1…").arg(pane.up.assetName)
                 : pane.up.state === "checking" ? qsTr("Checking…")
                 : pane.up.state === "unconfigured" ? qsTr("No update source is set")
                 : pane.up.state === "error" ? qsTr("The update check failed")
                 : qsTr("Talos %1").arg(pane.up.currentVersion)
            subtitle: pane.up.state === "error" ? pane.up.error
                    : pane.up.state === "unconfigured" ? qsTr("You are on %1. Name the repository that publishes this app's releases below.").arg(pane.up.currentVersion)
                    : pane.up.state === "ready"
                      ? (pane.up.verified ? qsTr("Checked against the release's published SHA-256.")
                                          : qsTr("This release publishes no checksum, so the download could not be verified."))
                    : pane.up.state === "available"
                      ? (pane.up.assetName !== "" ? qsTr("%1 · %2. Nothing downloads until you ask.").arg(pane.up.assetName).arg(pane.up.assetSize)
                                                  : qsTr("No build for this platform is attached; open the release page."))
                    : pane.up.state === "current" ? qsTr("You have the latest version. Last checked %1.").arg(pane.up.lastChecked)
                    : qsTr("You are on %1.").arg(pane.up.currentVersion)

            TButton {
                visible: pane.up.state === "available"
                compact: true
                kind: "primary"
                text: pane.up.assetName !== "" && App.platform !== "android" ? qsTr("Download") : qsTr("Open release")
                onClicked: pane.up.download()
            }
            TButton {
                visible: pane.up.state === "downloading"
                compact: true
                text: qsTr("Cancel")
                onClicked: pane.up.cancel()
            }
            TButton {
                visible: pane.up.state === "ready"
                compact: true
                kind: "primary"
                text: App.platform === "linux" ? qsTr("Show in folder") : qsTr("Install")
                onClicked: pane.up.install()
            }
            TButton {
                visible: ["idle", "current", "error"].indexOf(pane.up.state) >= 0
                compact: true
                text: qsTr("Check now")
                onClicked: pane.up.check()
            }
        }
        Item {
            visible: pane.up.state === "downloading"
            Layout.fillWidth: true
            implicitHeight: 14

            Rectangle {
                x: 14
                width: parent.width - 28
                height: 5
                radius: 2.5
                color: Theme.sunken

                Rectangle {
                    width: parent.width * pane.up.progress
                    height: parent.height
                    radius: 2.5
                    color: Theme.accent
                }
            }
        }
        SettingRow {
            visible: pane.up.available && pane.up.notes !== ""
            title: qsTr("What's new")
            subtitle: pane.up.notes
        }
        SettingRow {
            title: qsTr("Check automatically")
            subtitle: qsTr("At launch and every six hours. Only looks; never downloads.")
            TSwitch {
                checked: App.prefs.updateAutoCheck
                onToggled: App.prefs.updateAutoCheck = checked
            }
        }
        SettingRow {
            title: qsTr("Release repository")
            subtitle: qsTr("GitHub owner/name whose releases carry this app.")
            TField {
                implicitWidth: 220
                text: App.prefs.updateRepo
                placeholderText: "owner/repository"
                onEditingFinished: App.prefs.updateRepo = text.trim()
            }
        }
    }
}
