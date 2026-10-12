// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import Talos.Native

// A claim on the app's one slow clock, Theme.tick. Everything that shows
// "still going" (a spinner, the waiting dots, the connecting light) steps on
// that clock instead of animating by itself, so however many of them are on
// screen the window is redrawn eight times a second, not once per display
// frame for each. The clock only runs while someone holds a claim.
QtObject {
    id: beat

    /// Whether this one is showing and needs the clock.
    property bool active: false
    property bool held: false

    function sync() {
        if (active === held)
            return
        held = active
        Theme.beats += active ? 1 : -1
    }

    onActiveChanged: sync()
    Component.onCompleted: sync()
    Component.onDestruction: {
        if (held)
            Theme.beats -= 1
    }
}
