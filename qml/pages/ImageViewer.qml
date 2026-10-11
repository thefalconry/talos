// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Controls.Basic as C
import Talos.Native

// An attached image at full size.
C.Popup {
    id: viewer

    property string path
    property string name

    function show(p, n) {
        path = p
        name = n
        open()
    }

    parent: C.Overlay.overlay
    modal: true
    focus: true
    padding: 0
    width: parent.width
    height: parent.height
    closePolicy: C.Popup.CloseOnEscape

    C.Overlay.modal: Rectangle { color: "#E6000000" }
    background: null

    contentItem: Item {
        TapHandler { onTapped: viewer.close() }

        RemoteImage {
            anchors.fill: parent
            anchors.margins: 32
            path: viewer.visible ? viewer.path : ""
            name: viewer.name
        }
        Row {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 12
            spacing: 4

            IconButton {
                iconName: "download"
                iconColor: "#FFFFFF"
                tip: qsTr("Save to Downloads")
                onClicked: App.media.saveTo(viewer.path, viewer.name,
                                            "file://" + App.media.downloadsDir())
            }
            IconButton {
                iconName: "x"
                iconColor: "#FFFFFF"
                tip: qsTr("Close")
                onClicked: viewer.close()
            }
        }
    }
}
