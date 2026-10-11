// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import Talos.Native

// Settings: a section list beside the section's content on a wide window,
// one after the other on a phone.
Rectangle {
    id: page

    readonly property var win: C.ApplicationWindow.window
    readonly property bool narrow: win.narrow
    // On a phone the list comes first; picking a section shows it.
    property bool showList: !win.settingsDirect

    readonly property var sections: [
        { id: "connection", label: qsTr("Connection"), icon: "server", group: qsTr("Talon") },
        { id: "agent", label: qsTr("Agent"), icon: "spark", group: qsTr("Talon") },
        { id: "extensions", label: qsTr("Plugins & skills"), icon: "plug", group: qsTr("Talon") },
        { id: "memory", label: qsTr("Memory"), icon: "database", group: qsTr("Talon") },
        { id: "mesh", label: qsTr("Devices & mesh"), icon: "phone", group: qsTr("Talon") },
        { id: "logs", label: qsTr("Logs"), icon: "list", group: qsTr("Talon") },
        { id: "appearance", label: qsTr("Appearance"), icon: "palette", group: qsTr("This app") },
        { id: "general", label: qsTr("General"), icon: "settings", group: qsTr("This app") },
        { id: "security", label: qsTr("Security"), icon: "shield", group: qsTr("This app") },
        { id: "voice", label: qsTr("Voice"), icon: "volume", group: qsTr("This app") },
        { id: "updates", label: qsTr("Updates"), icon: "download", group: qsTr("This app") },
        { id: "about", label: qsTr("About"), icon: "info", group: qsTr("This app") }
    ]
    readonly property var current: {
        for (const s of sections) {
            if (s.id === win.settingsSection)
                return s
        }
        return sections[0]
    }
    readonly property var sources: ({
        "connection": "SettingsConnection.qml", "agent": "SettingsAgent.qml",
        "extensions": "SettingsExtensions.qml", "memory": "SettingsMemory.qml",
        "mesh": "SettingsMesh.qml", "logs": "SettingsLogs.qml",
        "appearance": "SettingsAppearance.qml", "general": "SettingsGeneral.qml",
        "security": "SettingsSecurity.qml", "voice": "SettingsVoice.qml",
        "updates": "SettingsUpdates.qml", "about": "SettingsAbout.qml"
    })

    color: Theme.bg

    // Eat clicks so nothing underneath reacts.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── Section list ────────────────────────────────────────────────────
        Rectangle {
            visible: !page.narrow || page.showList
            Layout.fillHeight: true
            Layout.fillWidth: page.narrow
            Layout.preferredWidth: 250
            color: page.narrow ? Theme.bg : Theme.sidebar

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 12
                    Layout.topMargin: 10
                    spacing: 4

                    IconButton {
                        iconName: "arrow-left"
                        tip: qsTr("Back to chats  (Esc)")
                        onClicked: page.win.settingsOpen = false
                    }
                    Text {
                        text: qsTr("Settings")
                        color: Theme.text
                        font.pixelSize: Theme.title
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                    }
                }

                ListView {
                    id: nav
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: 8
                    clip: true
                    model: page.sections
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Item {
                        id: entry
                        required property var modelData
                        required property int index
                        readonly property bool heads: index === 0
                                                      || page.sections[index - 1].group !== modelData.group
                        readonly property bool on: !page.narrow && modelData.id === page.current.id

                        width: nav.width
                        height: (heads ? 32 : 0) + (page.narrow ? 48 : 38)

                        Text {
                            visible: entry.heads
                            x: 20
                            y: 10
                            text: entry.modelData.group
                            color: Theme.textTertiary
                            font.pixelSize: Theme.small
                            font.weight: Font.Medium
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            height: page.narrow ? 46 : 36
                            radius: 9
                            color: entry.on ? Theme.selected : hover.hovered ? Theme.hover : "transparent"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 10
                                spacing: 11

                                Icon { name: entry.modelData.icon; size: 17; color: Theme.textSecondary }
                                Text {
                                    text: entry.modelData.label
                                    color: Theme.text
                                    font.pixelSize: Theme.body
                                    Layout.fillWidth: true
                                }
                                Rectangle {
                                    visible: entry.modelData.id === "updates" && App.updater.available
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: Theme.accent
                                }
                                Icon {
                                    visible: page.narrow
                                    name: "chevron-right"
                                    size: 15
                                    color: Theme.textTertiary
                                }
                            }
                            HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                onTapped: {
                                    page.win.settingsSection = entry.modelData.id
                                    page.showList = false
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                visible: !page.narrow
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }
        }

        // ── Section content ─────────────────────────────────────────────────
        ColumnLayout {
            visible: !page.narrow || !page.showList
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                Layout.leftMargin: page.narrow ? 8 : 28
                Layout.rightMargin: 12
                spacing: 4

                IconButton {
                    visible: page.narrow
                    iconName: "arrow-left"
                    tip: qsTr("Settings")
                    onClicked: page.showList = true
                }
                Text {
                    text: page.current.label
                    color: Theme.text
                    font.pixelSize: Theme.fs(20)
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    Layout.leftMargin: page.narrow ? 4 : 0
                }
            }

            Loader {
                id: content
                Layout.fillWidth: true
                Layout.fillHeight: true
                source: page.sources[page.current.id]
            }
        }
    }
}
