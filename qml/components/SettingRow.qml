// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts

// One row inside a Card: a label and explanation on the left, a control on
// the right. Rows separate themselves with a hairline.
Item {
    id: root

    property string title
    property string subtitle
    property string iconName
    property bool clickable: false
    property bool chevron: false
    property bool danger: false
    property bool first: false
    default property alias trailing: slot.data
    signal clicked()

    Layout.fillWidth: true
    implicitHeight: Math.max(52, layout.implicitHeight + 22)
    opacity: enabled ? 1 : 0.5

    // A row that does something is a button to a screen reader and to the
    // keyboard; one that only says something is read out.
    Accessible.role: clickable ? Accessible.Button : Accessible.StaticText
    Accessible.name: title
    Accessible.description: subtitle
    Accessible.onPressAction: if (clickable) clicked()
    activeFocusOnTab: clickable
    Keys.onReturnPressed: if (clickable) clicked()
    Keys.onSpacePressed: if (clickable) clicked()

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Theme.radius
        color: tap.pressed ? Theme.pressed : hover.hovered && root.clickable ? Theme.hover : "transparent"
        border.width: root.activeFocus ? 2 : 0
        border.color: Theme.accent
    }
    Rectangle {
        visible: !root.first
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        height: 1
        color: Theme.border
        opacity: 0.7
    }

    RowLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 12

        Icon {
            visible: root.iconName !== ""
            name: root.iconName
            size: 18
            color: root.danger ? Theme.danger : Theme.textSecondary
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                text: root.title
                color: root.danger ? Theme.danger : Theme.text
                font.pixelSize: Theme.body
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Text {
                visible: root.subtitle !== ""
                text: root.subtitle
                color: Theme.textSecondary
                font.pixelSize: Theme.small + 1
                wrapMode: Text.Wrap
                lineHeight: 1.2
                Layout.fillWidth: true
            }
        }
        RowLayout {
            id: slot
            spacing: 8
        }
        Icon {
            visible: root.chevron
            name: "chevron-right"
            size: 16
            color: Theme.textTertiary
        }
    }

    HoverHandler {
        id: hover
        enabled: root.clickable
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        id: tap
        enabled: root.clickable
        onTapped: root.clicked()
    }
}
