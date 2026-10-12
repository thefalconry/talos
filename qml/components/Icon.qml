// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import Talos.Native

// A stroke icon by name (see Theme.icons), drawn as a vector at any size.
VectorPath {
    id: root

    property string name
    property real size: 18

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    path: Theme.icons[name] || ""
    color: Theme.textSecondary
    weight: 1.7
    // Decoration: whatever it sits in carries the name.
    Accessible.ignored: true
}
