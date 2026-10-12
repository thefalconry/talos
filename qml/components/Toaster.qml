// SPDX-License-Identifier: GPL-2.0-only
import QtQuick

// One short message at the bottom of the window that goes away by itself.
Item {
    id: root

    function show(text) {
        if (!text)
            return
        label.text = text
        pill.opacity = 1
        hide.restart()
    }

    anchors.fill: parent
    z: 900

    Rectangle {
        id: pill
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 28
        width: Math.min(label.implicitWidth + 32, parent.width - 32)
        height: label.implicitHeight + 20
        radius: 12
        color: Theme.dark ? "#F3F1E9" : "#2B2A27"
        opacity: 0
        visible: opacity > 0

        // Announced when it appears, like the alert it is.
        Accessible.role: Accessible.AlertMessage
        Accessible.name: label.text

        Behavior on opacity { NumberAnimation { duration: 160 } }

        Text {
            id: label
            anchors.centerIn: parent
            width: Math.min(implicitWidth, root.width - 64)
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.dark ? "#1F1E1B" : "#F3F1E9"
            font.pixelSize: Theme.body
        }
    }
    Timer {
        id: hide
        interval: 3600
        onTriggered: pill.opacity = 0
    }
}
