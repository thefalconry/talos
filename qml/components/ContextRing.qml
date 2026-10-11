// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import Talos.Native

// How full the model's context window is, as a small ring. Click for the
// numbers and a way to start over.
Item {
    id: root

    property int pct: 0
    property bool warn: false
    property string label
    signal resetRequested()

    readonly property color tint: warn ? Theme.warning : Theme.textSecondary

    implicitWidth: 34
    implicitHeight: 34

    Rectangle {
        anchors.fill: parent
        radius: 9
        color: hover.hovered || popup.visible ? Theme.hover : "transparent"
    }
    VectorPath {
        width: 18
        height: 18
        anchors.centerIn: parent
        viewBox: 18
        weight: 2.4
        color: Theme.border
        path: "M9 1.5a7.5 7.5 0 1 0 0 15a7.5 7.5 0 1 0 0-15"
    }
    VectorPath {
        width: 18
        height: 18
        anchors.centerIn: parent
        viewBox: 18
        weight: 2.4
        color: root.tint
        // Clockwise from twelve o'clock; never a full circle, whose start
        // and end would coincide.
        path: {
            const turn = Math.max(0.02, Math.min(0.999, root.pct / 100))
            const a = turn * 2 * Math.PI
            const x = 9 + 7.5 * Math.sin(a)
            const y = 9 - 7.5 * Math.cos(a)
            return "M9 1.5A7.5 7.5 0 " + (turn > 0.5 ? 1 : 0) + " 1 " + x.toFixed(3) + " " + y.toFixed(3)
        }
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: popup.open() }

    C.ToolTip.visible: hover.hovered && !popup.visible
    C.ToolTip.delay: 600
    C.ToolTip.text: qsTr("Context: %1% used").arg(root.pct)

    C.Popup {
        id: popup
        x: root.width - width
        y: root.height + 6
        width: 270
        padding: 14

        background: Rectangle {
            radius: 12
            color: Theme.raised
            border.width: 1
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8

            Text {
                text: qsTr("Context window")
                color: Theme.text
                font.pixelSize: Theme.body
                font.weight: Font.DemiBold
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 6
                radius: 3
                color: Theme.sunken

                Rectangle {
                    width: parent.width * Math.min(100, root.pct) / 100
                    height: parent.height
                    radius: 3
                    color: root.warn ? Theme.warning : Theme.accent
                }
            }
            Text {
                text: qsTr("%1% used · %2").arg(root.pct).arg(root.label)
                color: Theme.textSecondary
                font.pixelSize: Theme.small + 1
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Text {
                visible: root.warn
                text: qsTr("Nearly full. Older parts of the conversation will be compacted or dropped soon.")
                color: Theme.warning
                font.pixelSize: Theme.small + 1
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            TButton {
                compact: true
                iconName: "refresh"
                text: qsTr("Start a fresh session")
                onClicked: {
                    popup.close()
                    root.resetRequested()
                }
            }
        }
    }
}
