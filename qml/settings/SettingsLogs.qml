// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

// Talon's log (from the daemon, operator access) and this app's own.
Pane {
    id: pane

    property string source: "daemon"
    property string level: "info"
    property var entries: []
    property string problem
    property bool loading: false
    property bool follow: false
    property var expanded: ({})

    readonly property var shown: {
        const q = filter.text.trim().toLowerCase()
        if (q === "")
            return entries
        return entries.filter(e => (e.msg || "").toLowerCase().includes(q)
                                   || (e.component || "").toLowerCase().includes(q))
    }

    function load() {
        problem = ""
        if (source === "app") {
            const floor = { "debug": 0, "info": 1, "warn": 2, "error": 3 }[level]
            entries = App.log.entries(floor === undefined ? 0 : floor)
            return
        }
        loading = true
        App.session.fetchLogs(400, level, "", function (r) {
            pane.loading = false
            pane.problem = r.error || ""
            if (!r.error)
                pane.entries = r.entries || []
        })
    }
    onSourceChanged: load()
    onLevelChanged: load()
    Component.onCompleted: load()

    Timer {
        interval: 3000
        repeat: true
        running: pane.follow
        onTriggered: pane.load()
    }

    Flow {
        Layout.fillWidth: true
        spacing: 8

        Segmented {
            value: pane.source
            options: [{ value: "daemon", label: qsTr("Talon") }, { value: "app", label: qsTr("This app") }]
            onChosen: value => pane.source = value
        }
        Segmented {
            value: pane.level
            options: [
                { value: "debug", label: qsTr("Debug") }, { value: "info", label: qsTr("Info") },
                { value: "warn", label: qsTr("Warnings") }, { value: "error", label: qsTr("Errors") }
            ]
            onChosen: value => pane.level = value
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        TField {
            id: filter
            Layout.fillWidth: true
            implicitHeight: 34
            placeholderText: qsTr("Filter by text or subsystem")
        }
        TButton {
            compact: true
            iconName: pane.follow ? "stop" : "refresh"
            text: pane.follow ? qsTr("Following") : qsTr("Refresh")
            busy: pane.loading && !pane.follow
            onClicked: pane.load()
        }
        TButton {
            compact: true
            text: pane.follow ? qsTr("Stop") : qsTr("Follow")
            onClicked: pane.follow = !pane.follow
        }
        IconButton {
            iconName: "copy"
            tip: qsTr("Copy what is shown")
            onClicked: {
                App.copy(pane.shown.map(e => App.dateTimeLabel(e.ts) + " " + e.level.toUpperCase()
                                             + " [" + (e.component || "") + "] " + e.msg
                                             + (e.err ? "\n  " + e.err : "")).join("\n"))
                pane.win.toast(qsTr("Copied %n line(s)", "", pane.shown.length))
            }
        }
    }

    Text {
        visible: pane.problem !== ""
        text: pane.problem + (pane.problem.indexOf("403") >= 0
              ? "\n" + qsTr("Talon's log holds command lines and paths, so it is only shown to a credential with operator access.") : "")
        color: Theme.danger
        font.pixelSize: Theme.body
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    Text {
        visible: pane.problem === "" && pane.shown.length === 0 && !pane.loading
        text: qsTr("No log lines at this level.")
        color: Theme.textSecondary
        font.pixelSize: Theme.body
    }

    Rectangle {
        visible: pane.shown.length > 0
        Layout.fillWidth: true
        implicitHeight: lines.implicitHeight + 12
        radius: Theme.radius + 2
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        Column {
            id: lines
            x: 6
            y: 6
            width: parent.width - 12

            Repeater {
                // Newest first; a log is read from the latest line back.
                model: Math.min(pane.shown.length, 400)

                Rectangle {
                    id: line
                    required property int index
                    readonly property var entry: pane.shown[pane.shown.length - 1 - index] || ({})
                    readonly property bool open: pane.expanded[entry.ts + entry.msg] === true
                    readonly property color tone: entry.level === "error" || entry.level === "fatal" ? Theme.danger
                                                : entry.level === "warn" ? Theme.warning : Theme.textTertiary
                    width: lines.width
                    height: lineBody.implicitHeight + 10
                    radius: 7
                    color: lineHover.hovered ? Theme.hover : "transparent"

                    Column {
                        id: lineBody
                        x: 8
                        y: 5
                        width: parent.width - 16
                        spacing: 3

                        Text {
                            width: parent.width
                            textFormat: Text.PlainText
                            wrapMode: line.open ? Text.WrapAnywhere : Text.NoWrap
                            elide: line.open ? Text.ElideNone : Text.ElideRight
                            color: Theme.text
                            font.family: Theme.mono
                            font.pixelSize: Theme.small
                            text: App.timeLabel(line.entry.ts) + "  "
                                  + (line.entry.level || "").toUpperCase().padEnd(5) + " "
                                  + (line.entry.component ? "[" + line.entry.component + "] " : "")
                                  + (line.entry.msg || "")
                        }
                        Text {
                            visible: (line.entry.err || "") !== ""
                            width: parent.width
                            textFormat: Text.PlainText
                            wrapMode: Text.WrapAnywhere
                            color: Theme.danger
                            font.family: Theme.mono
                            font.pixelSize: Theme.small
                            text: line.open && line.entry.stack ? line.entry.stack : (line.entry.err || "")
                        }
                    }
                    Rectangle {
                        width: 3
                        height: parent.height - 8
                        y: 4
                        radius: 1.5
                        color: line.tone
                        opacity: line.entry.level === "info" || line.entry.level === "debug" ? 0 : 1
                    }
                    HoverHandler { id: lineHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            const next = Object.assign({}, pane.expanded)
                            next[line.entry.ts + line.entry.msg] = !line.open
                            pane.expanded = next
                        }
                    }
                }
            }
        }
    }
}
