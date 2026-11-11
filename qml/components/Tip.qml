// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import Talos.Native

// A tooltip over its parent, for pointers that can hover. A phone gets
// nothing at all: there is no hovering there, and the stock tooltip is
// built from a Qt module that the Android package does not carry, so even
// asking about one fails, once for every button on screen.
Loader {
    id: tip

    property string text
    property bool show: false

    anchors.fill: parent
    active: !App.mobile
    sourceComponent: Item {
        C.ToolTip.visible: tip.show && tip.text !== ""
        C.ToolTip.delay: 600
        C.ToolTip.text: tip.text
    }
}
