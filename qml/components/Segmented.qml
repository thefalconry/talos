// SPDX-License-Identifier: GPL-2.0-only
import QtQuick

// A row of mutually exclusive choices.
Rectangle {
    id: root

    /// [{value, label}]
    property var options: []
    property var value
    signal chosen(var value)

    implicitHeight: 34
    implicitWidth: row.implicitWidth + 6
    radius: 9
    color: Theme.sunken
    border.width: 1
    border.color: Theme.border

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2

        Repeater {
            model: root.options

            Rectangle {
                id: cell
                required property var modelData
                readonly property bool on: root.value === modelData.value

                width: label.implicitWidth + 24
                height: root.height - 6
                radius: 7
                color: on ? Theme.surface : area.containsMouse ? Theme.hover : "transparent"
                border.width: on ? 1 : 0
                border.color: Theme.border

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: cell.modelData.label
                    color: cell.on ? Theme.text : Theme.textSecondary
                    font.pixelSize: Theme.small + 1
                    font.weight: cell.on ? Font.DemiBold : Font.Normal
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.chosen(cell.modelData.value)
                }
            }
        }
    }
}
