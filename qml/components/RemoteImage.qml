// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import Talos.Native

// An image the bridge serves. It is fetched with the session's credentials
// into a local cache and shown from there (see MediaStore).
Item {
    id: root

    property string path
    property string name
    property url local
    property bool failed: false
    property int fillMode: Image.PreserveAspectFit
    readonly property bool ready: picture.status === Image.Ready
    readonly property real aspect: ready && picture.implicitHeight > 0
                                   ? picture.implicitWidth / picture.implicitHeight : 1.6

    function resolve() {
        if (path === "")
            return
        failed = false
        const hit = App.media.cached(path, name)
        if (hit.toString() !== "")
            local = hit
        else
            App.media.fetch(path, name)
    }

    onPathChanged: resolve()
    Component.onCompleted: resolve()

    Connections {
        target: App.media
        function onReady(path, local) {
            if (path === root.path)
                root.local = local
        }
        function onFailed(path, error) {
            if (path === root.path)
                root.failed = true
        }
    }
    Connections {
        target: App.session
        function onConnChanged() {
            // A snapshot painted before the connection was up; try again now.
            if (App.session.conn === Session.Connected && root.local.toString() === "")
                root.resolve()
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: Theme.sunken
        visible: !root.ready

        Spinner {
            anchors.centerIn: parent
            visible: !root.failed
        }
        Icon {
            anchors.centerIn: parent
            visible: root.failed
            name: "image"
            color: Theme.textTertiary
            size: 22
        }
    }
    Image {
        id: picture
        anchors.fill: parent
        source: root.local
        asynchronous: true
        fillMode: root.fillMode
        sourceSize.width: 1600
        smooth: true
        mipmap: true
    }
}
