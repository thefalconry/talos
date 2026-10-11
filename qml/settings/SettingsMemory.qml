// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

// What Talon remembers. Read-only: the bridge lets a client look, never
// write, so nothing here can change a memory.
Pane {
    id: pane

    property var rows: []
    property string problem
    property bool loading: false
    property string kind
    property var kinds: []
    property var opened: null

    function load() {
        loading = true
        problem = ""
        App.session.fetchMemory(search.text, kind, 200, function (r) {
            pane.loading = false
            pane.rows = r.rows || []
            pane.problem = r.error || ""
            if (pane.kind === "" && search.text === "") {
                const seen = []
                for (const row of pane.rows) {
                    if (seen.indexOf(row.kind) < 0)
                        seen.push(row.kind)
                }
                pane.kinds = seen
            }
        })
    }
    Component.onCompleted: load()

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        TField {
            id: search
            Layout.fillWidth: true
            placeholderText: qsTr("Search what Talon remembers")
            onAccepted: pane.load()
        }
        TButton { iconName: "search"; text: qsTr("Search"); onClicked: pane.load() }
    }
    Flow {
        visible: pane.kinds.length > 1
        Layout.fillWidth: true
        spacing: 6

        Repeater {
            model: [""].concat(pane.kinds)

            Rectangle {
                id: chip
                required property string modelData
                readonly property bool on: pane.kind === modelData
                width: chipLabel.implicitWidth + 22
                height: 28
                radius: 14
                color: on ? Theme.accentSoft : "transparent"
                border.width: 1
                border.color: on ? Theme.accent : Theme.border

                Text {
                    id: chipLabel
                    anchors.centerIn: parent
                    text: chip.modelData === "" ? qsTr("All") : chip.modelData
                    color: Theme.text
                    font.pixelSize: Theme.small + 1
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        pane.kind = chip.modelData
                        pane.load()
                    }
                }
            }
        }
    }

    Spinner { visible: pane.loading; Layout.alignment: Qt.AlignHCenter }
    Text {
        visible: pane.problem !== "" || (!pane.loading && pane.rows.length === 0)
        text: pane.problem !== "" ? pane.problem
            : qsTr("Nothing here. Either Talon has no memories that match, or its typed memory store is not in use.")
        color: pane.problem !== "" ? Theme.danger : Theme.textSecondary
        font.pixelSize: Theme.body
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    Card {
        visible: pane.rows.length > 0

        Repeater {
            model: pane.rows

            SettingRow {
                id: memoryRow
                required property var modelData
                required property int index
                first: index === 0
                clickable: true
                chevron: true
                iconName: modelData.pinned ? "pin" : ""
                title: modelData.text
                subtitle: modelData.kind + " · " + modelData.subject
                          + (modelData.key ? " / " + modelData.key : "")
                          + " · " + qsTr("%1, %2% sure").arg(modelData.trust)
                                                         .arg(Math.round(modelData.confidence * 100))
                onClicked: {
                    pane.opened = { row: modelData, history: [] }
                    why.open()
                    App.session.fetchMemoryWhy(modelData.id, function (r) {
                        if (r.row)
                            pane.opened = r
                    })
                }
            }
        }
    }

    Sheet {
        id: why
        title: qsTr("Why Talon remembers this")
        maxWidth: 520
        readonly property var row: pane.opened ? pane.opened.row : ({})

        Text {
            text: why.row.text || ""
            color: Theme.text
            font.pixelSize: Theme.reading
            wrapMode: Text.Wrap
            lineHeight: 1.25
            Layout.fillWidth: true
        }
        Text {
            text: qsTr("%1 about %2 · trust: %3 · confidence %4% · recalled %5 times · last seen %6")
                  .arg(why.row.kind || "").arg(why.row.subject || "").arg(why.row.trust || "")
                  .arg(Math.round((why.row.confidence || 0) * 100)).arg(why.row.hitCount || 0)
                  .arg(App.relativeTime(why.row.lastSeenAt || 0))
            color: Theme.textSecondary
            font.pixelSize: Theme.small + 1
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Repeater {
            model: pane.opened ? pane.opened.history : []

            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 10

                Text {
                    text: App.dateTimeLabel(parent.modelData.at)
                    color: Theme.textTertiary
                    font.pixelSize: Theme.small
                    Layout.preferredWidth: 120
                }
                Text {
                    text: parent.modelData.op + (parent.modelData.reason ? ": " + parent.modelData.reason : "")
                    color: Theme.text
                    font.pixelSize: Theme.small + 1
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }
    }
}
