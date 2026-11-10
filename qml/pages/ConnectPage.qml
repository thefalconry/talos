// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import Talos.Native

// First run: tell the app where Talon is.
Rectangle {
    id: page
    color: Theme.bg

    readonly property var win: C.ApplicationWindow.window
    /// The height the page has without the keyboard. The form is placed
    /// against this, so it stays put when the keyboard comes up and the
    /// page scrolls instead: the field just tapped doesn't slide away.
    /// Hold steadyHeight while the keyboard is visible so an OS adjustResize
    /// shrink does not shift card.y or contentHeight mid-session.
    property real steadyHeight: height
    function updateSteadyHeight() {
        const full = win ? height + win.insetBottom - win.barBottom : height
        if (!Qt.inputMethod.visible || full > steadyHeight)
            steadyHeight = full
    }
    onHeightChanged: updateSteadyHeight()
    onWidthChanged: steadyHeight = win ? height + win.insetBottom - win.barBottom : height
    Connections {
        target: Qt.inputMethod
        function onVisibleChanged() {
            if (!Qt.inputMethod.visible)
                page.updateSteadyHeight()
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: card.y + card.implicitHeight + 32
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ColumnLayout {
            id: card
            width: Math.min(parent.width - 40, 460)
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(32, (page.steadyHeight - implicitHeight) / 2.6)
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
                text: App.mobile
                      ? qsTr("This app talks to a Talon daemon running the native bridge, anywhere this phone can reach.")
                      : qsTr("This app talks to a Talon daemon running the native bridge, on this computer or anywhere you can reach.")
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
