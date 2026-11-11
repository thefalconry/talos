// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts

// A modal card: centred on a wide window, rising from the bottom on a phone.
C.Popup {
    id: root

    property string title
    property int maxWidth: 480
    property bool dismissable: true
    default property alias content: body.data
    property alias footer: footerRow.data

    readonly property bool narrow: parent ? parent.width < Theme.narrowBelow : false

    parent: C.Overlay.overlay
    modal: true
    focus: true
    padding: 0
    closePolicy: dismissable ? C.Popup.CloseOnEscape | C.Popup.CloseOnPressOutside
                             : C.Popup.NoAutoClose
    readonly property var win: C.ApplicationWindow.window
    // What the status bar, the navigation bar (gestures or buttons, at the
    // bottom or the side) and the keyboard leave of the window.
    readonly property real roomTop: win ? win.overTop : 0
    readonly property real roomBottom: win ? win.overBottom : 0
    readonly property real roomLeft: win ? win.overLeft : 0
    readonly property real roomRight: win ? win.overRight : 0
    readonly property real gap: narrow ? 8 : 24
    onAboutToShow: if (win && win.popupOpened) win.popupOpened(root)
    onClosed: if (win && win.popupClosed) win.popupClosed(root)
    readonly property real roomWidth: (parent ? parent.width : 0) - roomLeft - roomRight
    readonly property real roomHeight: (parent ? parent.height : 0) - roomTop - roomBottom

    width: Math.min(roomWidth - 2 * gap, maxWidth)
    // Too tall for the room left (the keyboard is up): the body scrolls.
    height: Math.min(implicitHeight, roomHeight - 2 * gap)
    x: roomLeft + Math.round((roomWidth - width) / 2)
    // On a phone it rises from the bottom, above the navigation bar and
    // the keyboard; elsewhere it sits a little above the middle.
    y: narrow ? roomTop + roomHeight - height - gap
              : roomTop + Math.max(gap, Math.round((roomHeight - height) / 2.4))

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 110 }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 }
    }

    C.Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            visible: root.title !== ""
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 10
            Layout.topMargin: 12
            spacing: 8

            Text {
                text: root.title
                color: Theme.text
                font.pixelSize: Theme.title
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            IconButton {
                visible: root.dismissable
                iconName: "x"
                tip: qsTr("Close")
                onClicked: root.close()
            }
        }

        Flickable {
            id: scroller
            Layout.fillWidth: true
            Layout.topMargin: root.title !== "" ? 6 : 20
            Layout.bottomMargin: footerRow.children.length > 0 ? 0 : 20
            implicitHeight: Math.min(body.implicitHeight,
                                     (root.parent ? root.parent.height : 600) * 0.68)
            Layout.fillHeight: true
            Layout.minimumHeight: Math.min(body.implicitHeight, 48)
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: body
                x: 20
                width: scroller.width - 40
                spacing: 12
            }

            C.ScrollBar.vertical: C.ScrollBar { }
        }

        // Actions sit at the trailing edge, the main one outermost.
        RowLayout {
            visible: footerRow.children.length > 0
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.topMargin: 14
            spacing: 0

            Item { Layout.fillWidth: true }
            RowLayout {
                id: footerRow
                spacing: 8
                layoutDirection: Qt.RightToLeft
            }
        }
    }
}
