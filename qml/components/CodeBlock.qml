// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import Talos.Native

// A fenced code block: language label, copy button, highlighted source that
// scrolls sideways instead of wrapping.
Rectangle {
    id: root

    property string code
    property string language
    property bool closed: true

    implicitHeight: header.height + scroller.height + 1
    radius: Theme.radius
    color: Theme.sunken
    border.width: 1
    border.color: Theme.border
    clip: true

    Item {
        id: header
        width: parent.width
        height: 34

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: root.language !== "" ? root.language : qsTr("code")
            color: Theme.textTertiary
            font.pixelSize: Theme.small
            font.family: Theme.mono
        }
        TButton {
            id: copy
            property bool done: false
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            kind: "ghost"
            compact: true
            iconName: done ? "check" : "copy"
            text: done ? qsTr("Copied") : qsTr("Copy")
            onClicked: {
                App.copy(root.code)
                done = true
                reset.restart()
            }
            Timer { id: reset; interval: 1600; onTriggered: copy.done = false }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
            opacity: 0.7
        }
    }

    Flickable {
        id: scroller
        anchors.top: header.bottom
        width: parent.width
        height: source.implicitHeight + 24
        contentWidth: source.implicitWidth + 28
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width
        clip: true

        TextEdit {
            id: source
            x: 14
            y: 12
            text: root.code
            readOnly: true
            selectByMouse: !App.mobile
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.NoWrap
            color: Theme.text
            font.family: Theme.mono
            font.pixelSize: Theme.small + 1
            selectionColor: Theme.accentSoft
            selectedTextColor: Theme.text
            tabStopDistance: 4 * font.pixelSize * 0.6
        }

        C.ScrollBar.horizontal: C.ScrollBar { height: 8 }
    }

    CodeHighlighter {
        document: source.textDocument
        language: root.language
        dark: Theme.dark
    }
}
