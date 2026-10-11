// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C

C.Menu {
    id: control

    padding: 5
    implicitWidth: 230

    background: Rectangle {
        implicitWidth: 230
        radius: 12
        color: Theme.raised
        border.width: 1
        border.color: Theme.border
    }

    delegate: TMenuItem { }
}
