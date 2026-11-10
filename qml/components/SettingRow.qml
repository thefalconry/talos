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

    // On a narrow window a wide control (a row of choices) would squeeze
    // the label into a few letters a line: there it goes underneath.
    TextMetrics {
        id: titleSize
        font.pixelSize: Theme.body
        text: root.title
    }
    readonly property real textRoom: layout.width - slot.implicitWidth - layout.columnSpacing
                                     - (root.iconName !== "" ? 18 + layout.columnSpacing : 0)
                                     - (root.chevron ? 16 + layout.columnSpacing : 0)
    readonly property bool stacked: slot.implicitWidth > 0
                                    && textRoom < Math.min(titleSize.advanceWidth + 2, 150)

    GridLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        columnSpacing: 12
        rowSpacing: 8
        columns: 4

        Icon {
            visible: root.iconName !== ""
            name: root.iconName
            size: 18
            color: root.danger ? Theme.danger : Theme.textSecondary
            Layout.row: 0
            Layout.column: 0
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.row: 0
            Layout.column: 1
            Layout.columnSpan: root.stacked ? 2 : 1
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
            Layout.row: root.stacked ? 1 : 0
            Layout.column: root.stacked ? 1 : 2
            Layout.columnSpan: root.stacked ? 2 : 1
            Layout.maximumWidth: root.stacked ? layout.width : -1
        }
        Icon {
            visible: root.chevron
            name: "chevron-right"
            size: 16
            color: Theme.textTertiary
            Layout.row: 0
            Layout.column: 3
        }
    }

    MouseHover {
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
