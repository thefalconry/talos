// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

// Who we are connected to and whether that is working, in one line.
Item {
    id: root

    signal clicked()

    readonly property int conn: App.session.conn
    readonly property string label: {
        if (conn === Session.Connected)
            return App.session.endpoint.name || qsTr("Connected")
        if (conn === Session.Connecting)
            return App.session.connNote || qsTr("Connecting…")
        if (conn === Session.Error)
            return App.session.connFatal ? qsTr("Needs attention") : qsTr("Reconnecting…")
        return qsTr("Not connected")
    }

    implicitHeight: 34

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Connection: %1").arg(label)
    Accessible.onPressAction: clicked()
    activeFocusOnTab: true
    Keys.onReturnPressed: clicked()
    Keys.onSpacePressed: clicked()

    Rectangle {
        anchors.fill: parent
        radius: 9
        color: tap.pressed ? Theme.pressed : hover.hovered ? Theme.hover : "transparent"
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 8
        spacing: 8

        Rectangle {
            width: 8
            height: 8
            radius: 4
            color: root.conn === Session.Connected ? Theme.success
                 : root.conn === Session.Error && App.session.connFatal ? Theme.danger
                 : Theme.warning

            // Blinks rather than fades while a connection is being made:
            // a retry can go on for hours, and a fade would have the window
            // redrawn on every frame of them.
            opacity: blink.active && Math.floor(Theme.tick / 5) % 2 === 1 ? 0.35 : 1
            Beat {
                id: blink
                active: (root.conn === Session.Connecting
                         || (root.conn === Session.Error && !App.session.connFatal))
                        && root.Window.visibility !== Window.Hidden
                        && root.Window.visibility !== Window.Minimized
            }
        }
        Text {
            text: root.label
            color: Theme.textSecondary
            font.pixelSize: Theme.small + 1
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { id: tap; onTapped: root.clicked() }
}
