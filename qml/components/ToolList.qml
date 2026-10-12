// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

// The tool calls of one turn, folded into a single line that says what they
// amounted to ("Ran 3 commands, read 2 files · 4.2s") or, while one is in
// flight, what is happening now. A click unfolds the calls, one row each;
// a click on a row opens its input, output and error. A failure unfolds the
// list by itself, so it is never hidden behind a click.
//
// With "Say what tool calls are for" on, the line and the rows carry a short
// note of each call in place of the raw command: the model's own reason
// where it gave one ("Check which Qt is installed"), otherwise what the call
// acted on ("Read Session.cpp").
Column {
    id: root

    /// Rows from ToolCall::toVariant().
    property var tools: []

    readonly property var summary: App.toolSummary(tools)
    readonly property bool sayWhy: App.prefs.toolPurposes
    readonly property bool busy: summary.running.name !== undefined
    readonly property bool failed: summary.failed > 0
    property bool open: false
    /// The reader chose; nothing unfolds or folds on its own after that.
    property bool chosen: false

    spacing: 4

    onFailedChanged: {
        if (failed && !chosen)
            open = true
    }
    Component.onCompleted: {
        if (failed)
            open = true
    }

    /// What a call is doing, for the line shown while it runs.
    function doing(tool) {
        switch (tool.kind) {
        case "command": return qsTr("Running a command")
        case "read": return qsTr("Reading a file")
        case "edit": return qsTr("Editing a file")
        case "search": return qsTr("Searching files")
        case "web": return qsTr("Checking the web")
        case "agent": return qsTr("Running a subtask")
        }
        return tool.label || qsTr("Using a tool")
    }

    /// A short note of what a call was: the model's own line where it gave
    /// one, else the kind of work and what it was done to.
    function note(tool) {
        if ((tool.purpose || "") !== "")
            return tool.purpose
        const on = tool.detail || ""
        const file = on.split("/").pop()
        switch (tool.kind) {
        case "read": return on !== "" ? qsTr("Read %1").arg(file) : qsTr("Read a file")
        case "edit": return on !== "" ? qsTr("Edit %1").arg(file) : qsTr("Edit a file")
        case "search": return on !== "" ? qsTr("Search for %1").arg(on) : qsTr("Search the files")
        case "web": return on !== "" ? qsTr("Look up %1").arg(on) : qsTr("Check the web")
        case "command": return on !== "" ? on : qsTr("Run a command")
        case "agent": return on !== "" ? on : qsTr("Run a subtask")
        }
        return on !== "" ? (tool.label || "") + ": " + on : (tool.label || "")
    }

    /// The notes of all the calls in order, each once, with how many calls
    /// it covers: `[{text, count}]`.
    readonly property var notes: {
        const out = []
        for (let i = 0; i < tools.length; ++i) {
            const text = note(tools[i])
            if (text === "")
                continue
            const known = out.find(entry => entry.text === text)
            if (known)
                known.count += 1
            else
                out.push({ text: text, count: 1 })
        }
        return out
    }

    /// One kind of work and how often, as a clause of the finished line.
    function tally(bucket) {
        const n = bucket.count
        switch (bucket.kind) {
        case "command": return n === 1 ? qsTr("ran a command") : qsTr("ran %1 commands").arg(n)
        case "read": return n === 1 ? qsTr("read a file") : qsTr("read %1 files").arg(n)
        case "edit": return n === 1 ? qsTr("edited a file") : qsTr("edited %1 files").arg(n)
        case "search": return n === 1 ? qsTr("searched files") : qsTr("searched files %1 times").arg(n)
        case "web": return n === 1 ? qsTr("checked the web") : qsTr("checked the web %1 times").arg(n)
        case "agent": return n === 1 ? qsTr("ran a subtask") : qsTr("ran %1 subtasks").arg(n)
        }
        return n === 1 ? qsTr("used %1").arg(bucket.label) : qsTr("used %1 tools").arg(n)
    }

    readonly property string headline: {
        const s = summary
        if (s.total === 0)
            return ""
        if (busy) {
            const now = (sayWhy ? note(s.running) : doing(s.running)) + "…"
            return s.finished > 0 ? qsTr("%1 · %2 done").arg(now).arg(s.finished) : now
        }
        let text = ""
        if (sayWhy && notes.length > 0) {
            // The first notes, and how many calls they leave unsaid.
            const shown = notes.slice(0, 2)
            let rest = s.total
            for (const reason of shown)
                rest -= reason.count
            text = shown.map(reason => reason.text).join(", ")
            if (rest > 0)
                text = qsTr("%1 and %2 more").arg(text).arg(rest)
        } else {
            text = s.kinds.map(tally).join(", ")
            text = text.charAt(0).toUpperCase() + text.slice(1)
        }
        if (s.duration !== "")
            text += " · " + s.duration
        if (s.failed > 0)
            text += " · " + qsTr("%1 failed").arg(s.failed)
        return text
    }

    Rectangle {
        id: header
        visible: root.summary.total > 0
        width: Math.min(root.width, line.implicitWidth + 20)
        height: 30
        radius: 8
        color: headTap.pressed ? Theme.pressed : headHover.hovered ? Theme.hover : "transparent"

        Accessible.role: Accessible.Button
        Accessible.name: root.headline
        Accessible.description: root.open ? qsTr("Hide the tool calls") : qsTr("Show the tool calls")
        Accessible.onPressAction: root.toggle()

        RowLayout {
            id: line
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 7

            Item {
                implicitWidth: 15
                implicitHeight: 15

                Spinner {
                    anchors.centerIn: parent
                    visible: root.busy
                    size: 13
                    color: Theme.accent
                }
                Icon {
                    anchors.centerIn: parent
                    visible: !root.busy
                    name: root.failed ? "alert" : "tool"
                    size: 14
                    color: root.failed ? Theme.danger : Theme.textTertiary
                }
            }
            Text {
                text: root.headline
                color: Theme.textSecondary
                font.pixelSize: Theme.small + 1
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Icon {
                name: root.open ? "chevron-up" : "chevron-down"
                size: 13
                color: Theme.textTertiary
            }
        }
        HoverHandler { id: headHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { id: headTap; onTapped: root.toggle() }
    }

    function toggle() {
        open = !open
        chosen = true
    }

    Loader {
        width: root.width
        active: root.open
        visible: active
        sourceComponent: Column {
            spacing: 4

            Repeater {
                model: root.tools.length

                Column {
                    id: entry
                    required property int index
                    readonly property var tool: root.tools[index] || ({})
                    readonly property string note: root.note(tool)
                    readonly property bool reasoned: root.sayWhy && note !== ""
                    // With a single call there is nothing to choose between:
                    // unfolding the line shows what that call did.
                    property bool open: root.tools.length === 1

                    width: root.width
                    spacing: 0

                    Rectangle {
                        width: parent.width
                        height: 32
                        radius: 8
                        color: tap.pressed ? Theme.pressed : hover.hovered ? Theme.hover : "transparent"

                        Accessible.role: Accessible.Button
                        Accessible.name: entry.reasoned ? entry.note
                                                        : (entry.tool.label || "") + " " + (entry.tool.detail || "")
                        Accessible.onPressAction: entry.open = !entry.open

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 8

                            Item {
                                implicitWidth: 16
                                implicitHeight: 16

                                Spinner {
                                    anchors.centerIn: parent
                                    visible: !entry.tool.done && !entry.tool.failed
                                    size: 14
                                    color: Theme.accent
                                }
                                Icon {
                                    anchors.centerIn: parent
                                    visible: entry.tool.done === true || entry.tool.failed === true
                                    name: entry.tool.failed ? "alert" : "check"
                                    size: 15
                                    color: entry.tool.failed ? Theme.danger : Theme.textTertiary
                                }
                            }
                            // What it was for, then quietly which tool did it…
                            Text {
                                visible: entry.reasoned
                                text: entry.note
                                color: Theme.text
                                font.pixelSize: Theme.small + 1
                                elide: Text.ElideRight
                                Layout.fillWidth: entry.reasoned
                            }
                            // …or which tool, and on what.
                            Text {
                                text: entry.tool.label || ""
                                color: entry.reasoned ? Theme.textTertiary : Theme.text
                                font.pixelSize: entry.reasoned ? Theme.small : Theme.small + 1
                                font.weight: entry.reasoned ? Font.Normal : Font.Medium
                            }
                            Text {
                                visible: !entry.reasoned && (entry.tool.group || "") !== ""
                                text: entry.tool.group || ""
                                color: Theme.textTertiary
                                font.pixelSize: Theme.small
                            }
                            Text {
                                visible: !entry.reasoned
                                text: entry.tool.detail || ""
                                color: Theme.textSecondary
                                font.pixelSize: Theme.small + 1
                                font.family: Theme.mono
                                elide: Text.ElideRight
                                Layout.fillWidth: !entry.reasoned
                            }
                            Text {
                                visible: (entry.tool.duration || "") !== ""
                                text: entry.tool.duration || ""
                                color: Theme.textTertiary
                                font.pixelSize: Theme.small
                            }
                            Icon {
                                name: entry.open ? "chevron-up" : "chevron-down"
                                size: 14
                                color: Theme.textTertiary
                            }
                        }
                        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { id: tap; onTapped: entry.open = !entry.open }
                    }

                    Loader {
                        width: parent.width
                        active: entry.open
                        visible: active
                        sourceComponent: Column {
                            topPadding: 4
                            bottomPadding: 8
                            leftPadding: 32
                            spacing: 6

                            Repeater {
                                model: [
                                    { title: qsTr("Input"), body: entry.tool.input || "", bad: false },
                                    { title: qsTr("Output"), body: entry.tool.output || "", bad: false },
                                    { title: qsTr("Error"), body: entry.tool.error || "", bad: true }
                                ]

                                Column {
                                    id: part
                                    required property var modelData
                                    visible: modelData.body !== ""
                                    width: entry.width - 32
                                    spacing: 3

                                    Text {
                                        text: part.modelData.title
                                        color: part.modelData.bad ? Theme.danger : Theme.textTertiary
                                        font.pixelSize: Theme.small
                                        font.weight: Font.Medium
                                    }
                                    Rectangle {
                                        width: parent.width
                                        height: Math.min(bodyText.implicitHeight + 16, 220)
                                        radius: 8
                                        color: Theme.sunken
                                        clip: true

                                        Flickable {
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            contentHeight: bodyText.implicitHeight
                                            boundsBehavior: Flickable.StopAtBounds
                                            interactive: contentHeight > height

                                            TextEdit {
                                                id: bodyText
                                                width: parent.width
                                                text: part.modelData.body
                                                readOnly: true
                                                selectByMouse: !App.mobile
                                                wrapMode: TextEdit.WrapAnywhere
                                                color: part.modelData.bad ? Theme.danger : Theme.text
                                                font.family: Theme.mono
                                                font.pixelSize: Theme.small
                                                selectionColor: Theme.accentSoft
                                                selectedTextColor: Theme.text
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
