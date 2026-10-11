// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C

C.MenuItem {
    id: control

    property string iconName
    property bool danger: false
    property string hint

    implicitHeight: 34
    leftPadding: 10
    rightPadding: 10
    font.pixelSize: Theme.body
    hoverEnabled: true

    background: Rectangle {
        radius: 8
        color: control.highlighted || control.hovered ? Theme.hover : "transparent"
    }

    contentItem: Row {
        spacing: 9

        Icon {
            visible: control.iconName !== ""
            name: control.iconName
            size: 16
            color: control.danger ? Theme.danger : Theme.textSecondary
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: control.text
            font: control.font
            color: !control.enabled ? Theme.textTertiary
                 : control.danger ? Theme.danger : Theme.text
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth,
                            control.availableWidth - (control.iconName !== "" ? 25 : 0)
                            - (hintLabel.visible ? hintLabel.implicitWidth + 12 : 0))
        }
        Text {
            id: hintLabel
            visible: control.hint !== ""
            text: control.hint
            font.pixelSize: Theme.small
            color: Theme.textTertiary
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    indicator: Icon {
        visible: control.checked
        name: "check"
        size: 15
        color: Theme.accent
        x: control.width - width - 10
        y: (control.height - height) / 2
    }
    arrow: null
}
