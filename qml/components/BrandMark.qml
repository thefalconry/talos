// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import Talos.Native

// The app mark. Same geometry as the window icon and assets/talos.svg.
Item {
    id: root

    property real size: 28

    implicitWidth: size
    implicitHeight: size

    Rectangle {
        anchors.fill: parent
        anchors.margins: root.size * 2 / 64
        radius: root.size * 15 / 64
        color: "#1F1D1A"
        border.width: Theme.dark ? 1 : 0
        border.color: Theme.border
    }
    VectorPath {
        anchors.fill: parent
        viewBox: 64
        filled: true
        color: "#E07A45"
        path: "M17 20C30 16 44 22 47 36C48 42 45 47 40 49C43 42 40 35 33 32C36 38 33 44 27 46C30 39 26 33 19 31C22 28 22 24 17 20Z"
    }
}
