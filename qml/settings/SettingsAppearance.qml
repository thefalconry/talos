// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

Pane {
    id: pane

    Card {
        title: qsTr("Theme")

        SettingRow {
            first: true
            title: qsTr("Colours")
            Segmented {
                value: App.prefs.theme
                options: [
                    { value: "system", label: qsTr("System") },
                    { value: "light", label: qsTr("Light") },
                    { value: "dark", label: qsTr("Dark") }
                ]
                onChosen: value => App.prefs.theme = value
            }
        }
        SettingRow {
            title: qsTr("Accent")
            Row {
                spacing: 8

                Repeater {
                    model: Theme.accents

                    Rectangle {
                        id: swatch
                        required property string modelData
                        readonly property bool on: Qt.colorEqual(Theme.accent, modelData)
                        width: 26
                        height: 26
                        radius: 13
                        color: modelData
                        border.width: on ? 2 : 0
                        border.color: Theme.text

                        Rectangle {
                            visible: swatch.on
                            anchors.fill: parent
                            anchors.margins: 2
                            radius: 11
                            color: "transparent"
                            border.width: 2
                            border.color: Theme.surface
                        }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: App.prefs.accent = swatch.modelData }
                    }
                }
            }
        }
    }

    Card {
        title: qsTr("Reading")

        SettingRow {
            first: true
            title: qsTr("Text size")
            subtitle: qsTr("%1%").arg(Math.round(App.prefs.textScale * 100))
            Segmented {
                value: App.prefs.textScale
                options: [
                    { value: 0.9, label: qsTr("Small") }, { value: 1.0, label: qsTr("Default") },
                    { value: 1.12, label: qsTr("Large") }, { value: 1.25, label: qsTr("Larger") }
                ]
                onChosen: value => App.prefs.textScale = value
            }
        }
        SettingRow {
            title: qsTr("Show the model's reasoning as it thinks")
            subtitle: qsTr("Off keeps it folded under “Thinking…” until you open it.")
            TSwitch {
                checked: App.prefs.showReasoning
                onToggled: App.prefs.showReasoning = checked
            }
        }
    }

    // A live sample, so a change is seen without leaving the page.
    Card {
        title: qsTr("Preview")

        Item {
            Layout.fillWidth: true
            implicitHeight: sample.implicitHeight + 28

            MarkdownView {
                id: sample
                x: 16
                y: 14
                width: parent.width - 32
                blocks: [
                    { type: "text", text: qsTr("**Talon** replies look like this, with [links](https://example.com), `inline code` and lists:\n\n1. One\n2. Two") },
                    { type: "code", lang: "python", code: "def greet(name: str) -> str:\n    return f\"Hello, {name}!\"  # a comment", closed: true }
                ]
            }
        }
    }
}
