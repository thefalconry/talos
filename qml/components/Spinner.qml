// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import Talos.Native

// An indeterminate progress ring.
Item {
    id: root

    property real size: 18
    property color color: Theme.textSecondary
    property bool running: visible

    implicitWidth: size
    implicitHeight: size

    VectorPath {
        anchors.fill: parent
        // Three quarters of a circle.
        path: "M12 3a9 9 0 1 0 9 9"
        color: root.color
        weight: Math.max(1.6, root.size / 9)

        RotationAnimator on rotation {
            running: root.running
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
        }
    }
}
