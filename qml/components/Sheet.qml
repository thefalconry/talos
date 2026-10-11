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
    width: Math.min(parent.width - (narrow ? 16 : 48), maxWidth)
    x: Math.round((parent.width - width) / 2)
    y: narrow ? parent.height - height - 8
              : Math.max(24, Math.round((parent.height - height) / 2.4))

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
