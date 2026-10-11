// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import Talos.Native

// First run: tell the app where Talon is.
Rectangle {
    id: page
    color: Theme.bg

    Flickable {
        anchors.fill: parent
        contentHeight: card.implicitHeight + 64
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ColumnLayout {
            id: card
            width: Math.min(parent.width - 40, 460)
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(32, (page.height - implicitHeight) / 2.6)
            spacing: 18

            BrandMark {
                size: 52
                Layout.alignment: Qt.AlignHCenter
            }
            Text {
                text: qsTr("Connect to Talon")
                color: Theme.text
                font.family: Theme.serif
                font.pixelSize: Theme.fs(30)
                Layout.alignment: Qt.AlignHCenter
            }
            Text {
                text: qsTr("This app talks to a Talon daemon running the native bridge, on this computer or anywhere you can reach.")
                color: Theme.textSecondary
                font.pixelSize: Theme.body
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 1.3
                Layout.fillWidth: true
                Layout.bottomMargin: 6
            }
            ConnectionForm {
                Layout.fillWidth: true
            }
        }

        C.ScrollBar.vertical: C.ScrollBar { }
    }
}
