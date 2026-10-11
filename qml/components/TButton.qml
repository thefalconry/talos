// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Templates as T

T.Button {
    id: control

    // primary | secondary | ghost | danger
    property string kind: "secondary"
    property string iconName
    property bool compact: false
    property bool busy: false

    implicitHeight: compact ? 30 : 36
    implicitWidth: Math.max(implicitHeight, row.implicitWidth + (compact ? 22 : 30))
    font.pixelSize: compact ? Theme.small + 1 : Theme.body
    font.weight: Font.Medium
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    focusPolicy: Qt.TabFocus

    readonly property color ink: kind === "primary" ? Theme.accentInk
                               : kind === "danger" ? Theme.danger : Theme.text

    background: Rectangle {
        radius: 9
        color: {
            if (control.kind === "primary")
                return control.down ? Qt.darker(Theme.accent, 1.16)
                     : control.hovered ? Qt.darker(Theme.accent, 1.07) : Theme.accent
            if (control.down)
                return Theme.pressed
            if (control.hovered)
                return Theme.hover
            return control.kind === "ghost" ? "transparent" : Theme.surface
        }
        border.width: control.kind === "primary" || control.kind === "ghost" ? 0 : 1
        border.color: control.visualFocus ? Theme.accent
                    : control.kind === "danger" ? Qt.alpha(Theme.danger, 0.45) : Theme.border

        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: parent.radius + 2
            color: "transparent"
            border.width: 2
            border.color: Theme.accentSoft
            visible: control.visualFocus
        }
    }

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 7

            Spinner {
                visible: control.busy
                size: 14
                color: control.ink
                anchors.verticalCenter: parent.verticalCenter
            }
            Icon {
                visible: control.iconName !== "" && !control.busy
                name: control.iconName
                size: control.compact ? 14 : 16
                color: control.ink
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: text !== ""
                text: control.text
                font: control.font
                color: control.ink
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }
}
