// SPDX-License-Identifier: GPL-2.0-only
pragma Singleton

import QtQuick
import Talos.Native

// Colours, type and spacing for the whole app. Views never hard-code a
// colour: they read it here, so light/dark and the accent follow one switch.
QtObject {
    id: theme

    readonly property bool dark: App.prefs.theme === "dark"
                                 || (App.prefs.theme !== "light" && App.systemDark)
    readonly property real scale: Math.max(0.85, Math.min(1.4, App.prefs.textScale))
    readonly property color accent: App.prefs.accent

    // Surfaces, from the window back to raised cards.
    readonly property color bg: dark ? "#262624" : "#FAF9F5"
    readonly property color sidebar: dark ? "#1F1E1D" : "#F2F0E9"
    readonly property color surface: dark ? "#30302E" : "#FFFFFF"
    readonly property color raised: dark ? "#3A3936" : "#FFFFFF"
    readonly property color sunken: dark ? "#1C1B1A" : "#F1EFE8"
    readonly property color hover: dark ? "#14FFFFFF" : "#0D000000"
    readonly property color pressed: dark ? "#22FFFFFF" : "#17000000"
    // The row that is open or chosen wears the accent, faintly.
    readonly property color selected: Qt.rgba(accent.r, accent.g, accent.b, dark ? 0.20 : 0.13)
    readonly property color border: dark ? "#42413D" : "#E3DFD3"
    readonly property color borderStrong: dark ? "#5A5852" : "#CFCABB"
    readonly property color scrim: dark ? "#99000000" : "#59000000"

    readonly property color text: dark ? "#F3F1E9" : "#1F1E1B"
    readonly property color textSecondary: dark ? "#B9B6AA" : "#66635A"
    readonly property color textTertiary: dark ? "#8B887E" : "#928E82"
    readonly property color accentInk: "#FFFFFF"
    // Your own messages: the neutral bubble with a breath of the accent.
    readonly property color userBubble: Qt.tint(dark ? "#3A3936" : "#EFECE2",
                                                Qt.rgba(accent.r, accent.g, accent.b, dark ? 0.12 : 0.08))
    // A toast is the theme turned inside out, so it stands off the page.
    readonly property color toast: dark ? "#F3F1E9" : "#2B2A27"
    readonly property color toastText: dark ? "#1F1E1B" : "#F3F1E9"
    // The app mark: its tile, and the claw in the accent, lifted a little
    // so it reads on the dark tile.
    readonly property color markTile: "#1F1D1A"
    readonly property color mark: Qt.lighter(accent, 1.18)

    readonly property color danger: dark ? "#EC7D6F" : "#C0392B"
    readonly property color success: dark ? "#7CC48E" : "#2E7D4F"
    readonly property color warning: dark ? "#E6B45C" : "#B26A00"
    readonly property color accentSoft: Qt.rgba(accent.r, accent.g, accent.b, dark ? 0.22 : 0.13)

    readonly property string mono: Qt.platform.os === "windows" ? "Consolas"
                                 : Qt.platform.os === "osx" ? "Menlo" : "monospace"
    readonly property string serif: Qt.platform.os === "windows" || Qt.platform.os === "osx"
                                    ? "Georgia" : "serif"

    /// The colours the system's own parts are drawn in: the scroll bars,
    /// tooltips and selections of the stock controls, links in rich text,
    /// and anything Qt draws that is not one of this app's views.
    readonly property var system: ({
        "dark": dark, "window": bg, "base": surface, "alternate": sunken, "raised": raised,
        "text": text, "secondary": textSecondary, "tertiary": textTertiary,
        "border": border, "borderStrong": borderStrong,
        "accent": accent, "accentInk": accentInk
    })

    // The one slow clock that everything "still going" steps on; see Beat.
    property int beats: 0
    property int tick: 0
    readonly property Timer clock: Timer {
        interval: 125
        repeat: true
        running: theme.beats > 0
        onTriggered: theme.tick = (theme.tick + 1) % 240
    }

    function fs(px) { return Math.round(px * scale) }
    readonly property int small: fs(12)
    readonly property int body: fs(14)
    readonly property int reading: fs(15)
    readonly property int title: fs(17)

    readonly property int radius: 10
    readonly property int radiusLarge: 16
    readonly property int gap: 12
    readonly property int pad: 16
    // Wide enough to read comfortably, never wider.
    readonly property int column: 760
    readonly property int narrowBelow: 760

    readonly property var accents: [
        "#C2622D", "#B8862B", "#3E8E5A", "#2F7F8F", "#3D6FB8", "#7A5BC4", "#B8508A", "#5F6B7A"
    ]

    // Stroke icons on a 24 px grid.
    readonly property var icons: ({
        "plus": "M12 5v14M5 12h14",
        "x": "M6 6l12 12M18 6L6 18",
        "check": "M5 12.5l4.5 4.5L19 7.5",
        "search": "M11 4a7 7 0 1 0 0 14a7 7 0 0 0 0-14zM20 20l-4-4",
        "arrow-up": "M12 19V5M6 11l6-6l6 6",
        "arrow-down": "M12 5v14M6 13l6 6l6-6",
        "arrow-left": "M19 12H5M11 6l-6 6l6 6",
        "stop": "M7.5 7.5h9v9h-9z",
        "paperclip": "M20 11.5l-8.2 8.2a5 5 0 0 1-7.1-7.1l8.5-8.5a3.3 3.3 0 0 1 4.7 4.7l-8.4 8.4a1.7 1.7 0 0 1-2.4-2.4l7.6-7.6",
        "chevron-down": "M6 9l6 6l6-6",
        "chevron-up": "M6 15l6-6l6 6",
        "chevron-right": "M9 6l6 6l-6 6",
        "chevron-left": "M15 6l-6 6l6 6",
        "copy": "M9 9h10a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1H9a1 1 0 0 1-1-1V10a1 1 0 0 1 1-1zM5 15H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v1",
        "trash": "M4 7h16M10 11v6M14 11v6M6 7l1 12a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1l1-12M9 7V4h6v3",
        "edit": "M4 20h4L19 9l-4-4L4 16v4zM13.5 6.5l4 4",
        "menu": "M4 7h16M4 12h16M4 17h16",
        "sidebar": "M4 5h16a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1zM9 5v14",
        "more": "M5 12h.01M12 12h.01M19 12h.01",
        "refresh": "M20 11a8 8 0 0 0-14.3-4.3L4 8.5M4 4v4.5h4.5M4 13a8 8 0 0 0 14.3 4.3L20 15.5M20 20v-4.5h-4.5",
        "alert": "M12 4l9 16H3L12 4zM12 10v4M12 17h.01",
        "info": "M12 3a9 9 0 1 0 0 18a9 9 0 0 0 0-18zM12 11v5M12 8h.01",
        "lock": "M6 11h12a1 1 0 0 1 1 1v7a1 1 0 0 1-1 1H6a1 1 0 0 1-1-1v-7a1 1 0 0 1 1-1zM8 11V8a4 4 0 0 1 8 0v3",
        "mic": "M12 3a3 3 0 0 0-3 3v6a3 3 0 0 0 6 0V6a3 3 0 0 0-3-3zM5 11a7 7 0 0 0 14 0M12 18v3",
        "volume": "M4 10v4h3l5 4V6l-5 4H4zM16 9a4 4 0 0 1 0 6M18.5 6.5a8 8 0 0 1 0 11",
        "server": "M4 5h16v6H4zM4 13h16v6H4zM7.5 8h.01M7.5 16h.01",
        "monitor": "M4 5h16v11H4zM9 20h6M12 16v4",
        "phone": "M8 3h8a1 1 0 0 1 1 1v16a1 1 0 0 1-1 1H8a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1zM11 18h2",
        "file": "M7 3h7l5 5v12a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1zM14 3v5h5",
        "image": "M4 5h16v14H4zM4 16l5-5l4 4l3-3l4 4M9 9h.01",
        "download": "M12 4v11M7 11l5 5l5-5M5 20h14",
        "upload": "M12 20V9M7 13l5-5l5 5M5 4h14",
        "external": "M9 5H5v14h14v-4M13 5h6v6M19 5l-8 8",
        "spark": "M12 4l1.6 4.4L18 10l-4.4 1.6L12 16l-1.6-4.4L6 10l4.4-1.6L12 4zM18.5 15.5l.7 1.8l1.8.7l-1.8.7l-.7 1.8l-.7-1.8l-1.8-.7l1.8-.7l.7-1.8z",
        "tool": "M15 4a5 5 0 0 0-4.6 7L4 17.4V20h2.6l6.4-6.4A5 5 0 0 0 20 9l-3 3l-3-1l-1-3l3-3z",
        "terminal": "M4 5h16v14H4zM7.5 9.5l3 2.5l-3 2.5M12.5 15h4",
        "clock": "M12 3a9 9 0 1 0 0 18a9 9 0 0 0 0-18zM12 7.5V12l3 2",
        "shield": "M12 3l7 3v5c0 4.5-3 8-7 10c-4-2-7-5.5-7-10V6l7-3z",
        "bell": "M6 16V11a6 6 0 0 1 12 0v5l1.5 2h-15L6 16zM10 20a2 2 0 0 0 4 0",
        "sun": "M12 8a4 4 0 1 0 0 8a4 4 0 0 0 0-8zM12 2v2M12 20v2M2 12h2M20 12h2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4",
        "plug": "M9 3v5M15 3v5M6 8h12v4a6 6 0 0 1-12 0V8zM12 18v3",
        "list": "M8 7h12M8 12h12M8 17h12M4 7h.01M4 12h.01M4 17h.01",
        "database": "M5 6c0-1.7 3.1-3 7-3s7 1.3 7 3s-3.1 3-7 3s-7-1.3-7-3zM5 6v6c0 1.7 3.1 3 7 3s7-1.3 7-3V6M5 12v6c0 1.7 3.1 3 7 3s7-1.3 7-3v-6",
        "pin": "M12 21s-6.5-6-6.5-11a6.5 6.5 0 0 1 13 0c0 5-6.5 11-6.5 11zM12 8a2.3 2.3 0 1 0 0 4.6a2.3 2.3 0 0 0 0-4.6z",
        "link": "M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1",
        "key": "M15 4a5 5 0 1 0 0 10a5 5 0 0 0 0-10zM11.5 12.5L4 20M7 17l2.5 2.5",
        "power": "M12 3v9M7 6.5a7 7 0 1 0 10 0",
        "chat": "M5 5h14a1 1 0 0 1 1 1v9a1 1 0 0 1-1 1h-7l-5 4v-4H5a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1z",
        "keyboard": "M3 7h18v10H3zM7 11h.01M11 11h.01M15 11h.01M8 14h8",
        "zap": "M13 3L5 13h6l-1 8l8-10h-6l1-8z",
        "offline": "M3 3l18 18M8.5 16a5 5 0 0 1 7 0M5 12.5a10 10 0 0 1 3-2M12 20h.01M19 12.5a10 10 0 0 0-5-2.7",
        "eye": "M2.5 12s3.5-6.5 9.5-6.5s9.5 6.5 9.5 6.5s-3.5 6.5-9.5 6.5S2.5 12 2.5 12zM12 9a3 3 0 1 0 0 6a3 3 0 0 0 0-6z",
        "folder": "M4 6h5l2 2h9v11H4z",
        "pulse": "M3 12h4l3-7l4 14l3-7h4",
        "cpu": "M7 7h10v10H7zM10 3v4M14 3v4M10 17v4M14 17v4M3 10h4M3 14h4M17 10h4M17 14h4",
        "user": "M12 4a4 4 0 1 0 0 8a4 4 0 0 0 0-8zM5 20a7 7 0 0 1 14 0",
        "settings": "M4 7h9M17 7h3M4 12h3M11 12h9M4 17h11M19 17h1M15 5v4M9 10v4M17 15v4",
        "palette": "M12 3a9 9 0 1 0 0 18c1.4 0 2-1 2-2c0-1.2-.9-1.6-.9-2.6c0-.8.7-1.4 1.6-1.4H17a4 4 0 0 0 4-4c0-4.4-4-8-9-8zM7.5 12h.01M9.5 8h.01M14 7h.01"
    })
}
