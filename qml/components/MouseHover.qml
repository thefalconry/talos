// SPDX-License-Identifier: GPL-2.0-only
import QtQuick

// A HoverHandler for pointers that can hover. A finger can't: its tap would
// leave the thing it touched looking hovered until the next touch elsewhere.
HoverHandler {
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
}
