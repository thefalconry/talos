// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

// The tool calls of one turn. Each row says what ran and on what; a click
// opens its input, output and error.
Column {
    id: root

    /// Rows from ToolCall::toVariant().
    property var tools: []

    spacing: 4

    Repeater {
        model: root.tools.length

        Column {
            id: entry
            required property int index
            readonly property var tool: root.tools[index] || ({})
            property bool open: false

            width: root.width
            spacing: 0

            Rectangle {
                width: parent.width
                height: 32
                radius: 8
                color: tap.pressed ? Theme.pressed : hover.hovered ? Theme.hover : "transparent"

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
                            visible: !entry.tool.done
                            size: 14
                            color: Theme.accent
                        }
                        Icon {
                            anchors.centerIn: parent
                            visible: entry.tool.done === true
                            name: entry.tool.failed ? "alert" : "check"
                            size: 15
                            color: entry.tool.failed ? Theme.danger : Theme.textTertiary
                        }
                    }
                    Text {
                        text: entry.tool.label || ""
                        color: Theme.text
                        font.pixelSize: Theme.small + 1
                        font.weight: Font.Medium
                    }
                    Text {
                        visible: (entry.tool.group || "") !== ""
                        text: entry.tool.group || ""
                        color: Theme.textTertiary
                        font.pixelSize: Theme.small
                    }
                    Text {
                        text: entry.tool.detail || ""
                        color: Theme.textSecondary
                        font.pixelSize: Theme.small + 1
                        font.family: Theme.mono
                        elide: Text.ElideRight
                        Layout.fillWidth: true
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
