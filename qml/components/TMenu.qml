// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C

C.Menu {
    id: control

    padding: 5
    implicitWidth: 230
    // Kept clear of the status bar, the navigation bar and the keyboard.
    readonly property var win: C.ApplicationWindow.window
    topMargin: (win ? win.overTop : 0) + 8
    bottomMargin: (win ? win.overBottom : 0) + 8
    leftMargin: (win ? win.overLeft : 0) + 8
    rightMargin: (win ? win.overRight : 0) + 8
    onAboutToShow: if (win && win.popupOpened) win.popupOpened(control)
    onClosed: if (win && win.popupClosed) win.popupClosed(control)

    background: Rectangle {
        implicitWidth: 230
        radius: 12
        color: Theme.raised
        border.width: 1
        border.color: Theme.border
    }

    delegate: TMenuItem { }
}
