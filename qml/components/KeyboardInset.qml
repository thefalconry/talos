// SPDX-License-Identifier: GPL-2.0-only
import QtQuick

// How much of the window the on-screen keyboard covers, for a layout to make
// room for: the one number `overlap`, steadied against what Qt reports.
//
// Two things in Qt's account of the keyboard on Android can't be taken at
// face value:
//
//  - Moving focus from one field to another makes Qt hide the keyboard and
//    show it again. When the show comes before the hide has finished, the
//    system never reports the keyboard gone, so Qt keeps its "hiding" mark
//    and `Qt.inputMethod.visible` stays false with the keyboard on screen.
//    The keyboard's rectangle is not affected: it is emptied only when the
//    keyboard really goes. So while a field is being edited the rectangle
//    decides, and `visible` is only consulted when none is.
//  - The rectangle can be in device pixels, not in the units the window is
//    laid out in, wherever the screen's pixel ratio is above 1. A docked
//    keyboard ends at the bottom of the window, so a rectangle that ends well
//    below it must be in device pixels and is scaled by the pixel ratio.
QtObject {
    id: root

    /// The window's height, in the units its items are laid out in.
    property real windowHeight: 0
    /// Qt.inputMethod.keyboardRectangle.
    property rect keyboardRect: Qt.rect(0, 0, 0, 0)
    /// Qt.inputMethod.visible. Not trusted while a field is being edited.
    property bool inputVisible: false
    /// Whether a field that takes text has focus.
    property bool typing: false
    /// The screen's device pixel ratio.
    property real pixelRatio: 1

    /// What the keyboard covers this instant, as the system reports it:
    /// nothing, where the system has already made the window shorter (the
    /// keyboard then starts below it).
    readonly property real covered: {
        const r = keyboardRect
        if (r.height <= 0 || !(inputVisible || typing))
            return 0
        const scale = pixelRatio > 1 && r.y + r.height > windowHeight * 1.25
                      ? 1 / pixelRatio : 1
        return Math.max(0, Math.min(windowHeight, windowHeight - r.y * scale))
    }

    /// `covered`, steadied. Moving between fields, the keyboard may report
    /// itself gone for a moment and then back. Following that blink would
    /// grow the page, pull every scrolled view back to the top, and leave
    /// the field being edited behind the keyboard once it returns. So room
    /// for the keyboard is made at once, and given back only once the
    /// keyboard has stayed away.
    property real overlap: 0

    onCoveredChanged: {
        if (covered >= overlap) {
            giveBack.stop()
            overlap = covered
        } else {
            // Longer while a field is being edited: its keyboard is most
            // likely on its way back.
            giveBack.interval = typing ? 400 : 120
            giveBack.restart()
        }
    }

    readonly property Timer giveBack: Timer {
        onTriggered: root.overlap = root.covered
    }
}
