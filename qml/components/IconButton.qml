// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Templates as T

T.Button {
    id: control

    property string iconName
    property string tip
    property real size: 34
    property real iconSize: 18
    property real iconWeight: 1.7
    property color iconColor: Theme.textSecondary
    property bool filled: false
    property bool accent: false

    implicitWidth: size
    implicitHeight: size
    hoverEnabled: true
    opacity: enabled ? 1 : 0.4
    focusPolicy: Qt.TabFocus

    Accessible.name: tip

    background: Rectangle {
        radius: control.accent ? width / 2 : 9
        color: control.accent
               ? (control.down ? Qt.darker(Theme.accent, 1.16)
                  : control.hovered ? Qt.darker(Theme.accent, 1.07) : Theme.accent)
               : control.down ? Theme.pressed
               : control.hovered || control.checked ? Theme.hover : "transparent"
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.accentSoft
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            filled: control.filled
            weight: control.accent ? 2.1 : control.iconWeight
            color: control.accent ? Theme.accentInk
                 : control.hovered ? Theme.text : control.iconColor
        }
    }

    C.ToolTip.visible: hovered && tip !== "" && !down
    C.ToolTip.delay: 600
    C.ToolTip.text: tip

    HoverHandler { cursorShape: Qt.PointingHandCursor }
}
