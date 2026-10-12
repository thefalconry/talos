// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import Talos.Native

// An indeterminate progress ring.
//
// It turns in eight steps a second, on the app's shared clock, not smoothly.
// A smooth turn has the whole window redrawn on every frame the display
// shows (120 a second on some) for as long as a reply takes, which is
// minutes of a processor core's attention for a ring a few pixels wide.
// And it stands still while the window cannot be seen at all.
Item {
    id: root

    property real size: 18
    property color color: Theme.textSecondary
    property bool running: visible

    implicitWidth: size
    implicitHeight: size

    Beat {
        active: root.running && root.Window.visibility !== Window.Hidden
                && root.Window.visibility !== Window.Minimized
    }

    VectorPath {
        anchors.fill: parent
        // Three quarters of a circle.
        path: "M12 3a9 9 0 1 0 9 9"
        color: root.color
        weight: Math.max(1.6, root.size / 9)
        rotation: (Theme.tick % 8) * 45
    }
}
