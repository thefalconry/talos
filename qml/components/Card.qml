// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts

// A titled group of settings rows.
ColumnLayout {
    id: root

    property string title
    property string caption
    default property alias content: body.data

    spacing: 8
    Layout.fillWidth: true

    Text {
        visible: root.title !== ""
        text: root.title
        color: Theme.text
        font.pixelSize: Theme.body
        font.weight: Font.DemiBold
        Layout.leftMargin: 2
    }
    Text {
        visible: root.caption !== ""
        text: root.caption
        color: Theme.textSecondary
        font.pixelSize: Theme.small + 1
        wrapMode: Text.Wrap
        lineHeight: 1.25
        Layout.fillWidth: true
        Layout.leftMargin: 2
        Layout.bottomMargin: 2
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: body.implicitHeight
        radius: Theme.radius + 2
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        ColumnLayout {
            id: body
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 0
        }
    }
}
