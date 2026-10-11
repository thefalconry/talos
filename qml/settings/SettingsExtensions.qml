// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

// Plugins (MCP servers and modules) and skills (SKILL.md bundles).
Pane {
    id: pane

    property var plugins: []
    property var skills: []
    property string problem
    property bool loading: true
    property string busy

    function load() {
        loading = true
        problem = ""
        App.session.fetchPlugins(function (r) {
            pane.plugins = r.items || []
            if (r.error)
                pane.problem = r.error
            App.session.fetchSkills(function (s) {
                pane.skills = s.items || []
                if (s.error && !pane.problem)
                    pane.problem = s.error
                pane.loading = false
            })
        })
    }
    function toggle(kind, name, on) {
        busy = kind + ":" + name
        const done = function (r) {
            pane.busy = ""
            if (!r.ok)
                pane.win.toast(r.error || qsTr("Talon refused the change"))
            pane.load()
        }
        if (kind === "plugin")
            App.session.togglePlugin(name, on, done)
        else
            App.session.toggleSkill(name, on, done)
    }

    Component.onCompleted: load()

    Spinner { visible: pane.loading && pane.plugins.length === 0; Layout.alignment: Qt.AlignHCenter }
    Text {
        visible: pane.problem !== ""
        text: pane.problem
        color: Theme.danger
        font.pixelSize: Theme.body
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    Card {
        visible: pane.plugins.length > 0
        title: qsTr("Plugins")
        caption: qsTr("Tools Talon can use. Changes load into the running daemon straight away.")

        Repeater {
            model: pane.plugins

            SettingRow {
                id: pluginRow
                required property var modelData
                required property int index
                first: index === 0
                title: modelData.name
                subtitle: (modelData.kind === "builtin" ? qsTr("Built in")
                           : modelData.kind === "mcp" ? qsTr("MCP server") : qsTr("Module"))
                          + " · " + modelData.source

                Spinner { visible: pane.busy === "plugin:" + pluginRow.modelData.name; size: 16 }
                TSwitch {
                    checked: pluginRow.modelData.enabled
                    enabled: pane.busy === ""
                    onToggled: pane.toggle("plugin", pluginRow.modelData.name, checked)
                }
            }
        }
    }

    Card {
        visible: pane.skills.length > 0
        title: qsTr("Skills")
        caption: qsTr("Reusable workflows. A disabled skill stays installed but leaves the model's index; it applies from the next session.")

        Repeater {
            model: pane.skills

            SettingRow {
                id: skillRow
                required property var modelData
                required property int index
                first: index === 0
                title: modelData.name
                subtitle: modelData.description

                Spinner { visible: pane.busy === "skill:" + skillRow.modelData.name; size: 16 }
                TSwitch {
                    checked: skillRow.modelData.enabled
                    enabled: pane.busy === ""
                    onToggled: pane.toggle("skill", skillRow.modelData.name, checked)
                }
            }
        }
    }

    Text {
        visible: !pane.loading && pane.problem === "" && pane.plugins.length === 0 && pane.skills.length === 0
        text: qsTr("Nothing is installed. Add plugins and skills with `talon plugin install` and `talon skill install` on the machine Talon runs on.")
        color: Theme.textSecondary
        font.pixelSize: Theme.body
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
}
