// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts

// A scrolling settings section: a centred column of cards.
Flickable {
    id: pane

    default property alias content: column.data
    readonly property var win: C.ApplicationWindow.window

    contentHeight: column.implicitHeight + 48
    boundsBehavior: Flickable.StopAtBounds
    clip: true

    ColumnLayout {
        id: column
        x: pane.win.narrow ? 16 : 28
        y: 8
        width: Math.min(pane.width - 2 * x, 680)
        spacing: 22
    }

    C.ScrollBar.vertical: C.ScrollBar { }
}
