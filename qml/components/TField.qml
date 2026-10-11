// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C

C.TextField {
    id: control

    property bool invalid: false

    implicitHeight: 38
    implicitWidth: 240
    leftPadding: 12
    rightPadding: 12
    topPadding: 0
    bottomPadding: 0
    color: Theme.text
    placeholderTextColor: Theme.textTertiary
    selectionColor: Theme.accentSoft
    selectedTextColor: Theme.text
    font.pixelSize: Theme.body
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true
    opacity: enabled ? 1 : 0.5

    background: Rectangle {
        radius: 9
        color: Theme.surface
        border.width: control.activeFocus ? 1.5 : 1
        border.color: control.invalid ? Theme.danger
                    : control.activeFocus ? Theme.accent : Theme.border
    }
}
