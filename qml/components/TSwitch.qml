// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Templates as T

T.Switch {
    id: control

    implicitWidth: 40
    implicitHeight: 24
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    focusPolicy: Qt.TabFocus

    indicator: Rectangle {
        width: 40
        height: 24
        radius: 12
        color: control.checked ? Theme.accent : Theme.borderStrong
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.accentSoft

        Behavior on color { ColorAnimation { duration: 120 } }

        Rectangle {
            width: 18
            height: 18
            radius: 9
            y: 3
            x: control.checked ? 19 : 3
            color: "#FFFFFF"

            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }
}
