// SPDX-License-Identifier: GPL-2.0-only
//
// Lays out a disk image's window: its size, the picture behind it, and
// where the app and the Applications folder sit. Finder keeps all of that
// in the volume's .DS_Store, and the usual way to get one is to script
// Finder into arranging a window. That needs a desktop session and a
// permission prompt; this writes the file itself, so an image can be built
// from a terminal or a build machine.
//
//   swift dmg-layout.swift <mounted volume> <app name> <picture, relative to the volume>
import Foundation

let arguments = CommandLine.arguments
guard arguments.count == 4 else {
    FileHandle.standardError.write(Data("usage: dmg-layout.swift <volume> <app> <picture>\n".utf8))
    exit(64)
}
let volume = URL(fileURLWithPath: arguments[1], isDirectory: true)
let appName = arguments[2]
let picture = volume.appendingPathComponent(arguments[3])

// ── What the window should look like ────────────────────────────────────────

let windowWidth = 660
let windowHeight = 400
let titleBar = 28
let iconSize = 128
let positions: [(name: String, x: Int, y: Int)] = [
    (appName, 170, 190),
    ("Applications", 490, 190),
]

// ── The pieces of a .DS_Store ───────────────────────────────────────────────

func be32(_ value: Int) -> Data {
    withUnsafeBytes(of: UInt32(value).bigEndian) { Data($0) }
}

func utf16be(_ text: String) -> Data {
    var out = Data()
    for unit in text.utf16 {
        out.append(UInt8(unit >> 8))
        out.append(UInt8(unit & 0xff))
    }
    return out
}

/// One record: whose it is, which property, the type of the value, the value.
func record(_ file: String, _ property: String, _ type: String, _ value: Data) -> Data {
    be32(file.utf16.count) + utf16be(file) + Data(property.utf8) + Data(type.utf8) + value
}

func blob(_ bytes: Data) -> Data {
    be32(bytes.count) + bytes
}

func plist(_ dictionary: [String: Any]) throws -> Data {
    try PropertyListSerialization.data(fromPropertyList: dictionary, format: .binary, options: 0)
}

// The picture is named by a bookmark, which only the system can make, and
// only for a file that exists: the volume has to be mounted and filled.
// Finder reads it from a record of its own ("pBBk"); the alias older
// systems kept inside the icon view's settings is not needed, and the two
// together would not fit in the one node this file has.
let bookmark = try picture.bookmarkData(options: [], includingResourceValuesForKeys: nil, relativeTo: nil)

let window = try plist([
    "WindowBounds": "{{200, 120}, {\(windowWidth), \(windowHeight + titleBar)}}",
    "ShowSidebar": false,
    "ShowToolbar": false,
    "ShowStatusBar": false,
    "ShowPathbar": false,
    "ShowTabView": false,
    "ContainerShowSidebar": false,
    "PreviewPaneVisibility": false,
    "SidebarWidth": 0,
])
let icons = try plist([
    "viewOptionsVersion": 1,
    "arrangeBy": "none",
    "iconSize": Double(iconSize),
    "textSize": 13.0,
    "labelOnBottom": true,
    "gridSpacing": 100.0,
    "gridOffsetX": 0.0,
    "gridOffsetY": 0.0,
    "showIconPreview": true,
    "showItemInfo": false,
    "scrollPositionX": 0.0,
    "scrollPositionY": 0.0,
    "backgroundType": 2, // a picture
    "backgroundColorRed": 1.0,
    "backgroundColorGreen": 1.0,
    "backgroundColorBlue": 1.0,
])

// Sorted the way Finder keeps them: by file name without regard to case,
// then by property.
var records: [(file: String, property: String, data: Data)] = [
    (".", "bwsp", record(".", "bwsp", "blob", blob(window))),
    (".", "icvl", record(".", "icvl", "type", Data("icnv".utf8))),
    (".", "icvp", record(".", "icvp", "blob", blob(icons))),
    (".", "pBBk", record(".", "pBBk", "blob", blob(bookmark))),
    (".", "vSrn", record(".", "vSrn", "long", be32(1))),
]
for item in positions {
    // Where the icon's centre is, then Finder's "no particular order" filler.
    let place = be32(item.x) + be32(item.y) + Data([0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00])
    records.append((item.name, "Iloc", record(item.name, "Iloc", "blob", blob(place))))
}
records.sort {
    let a = $0.file.lowercased(), b = $1.file.lowercased()
    return a == b ? $0.property < $1.property : a < b
}

// ── The file around them ────────────────────────────────────────────────────
//
// A .DS_Store is a small heap of power-of-two blocks (a "buddy allocator")
// holding one B-tree. This one needs four blocks, placed by hand:
//
//   offset 0     32 bytes   the header
//   offset 32    32 bytes   the tree's own record: root node, counts
//   offset 2048  2 KiB      the allocator's table of blocks and free space
//   offset 4096  4 KiB      the tree's single node, holding every record
//
// A block's address is its offset with the log2 of its size in the low
// five bits. Offsets count from byte 4 of the file.

let pageSize = 4096
var node = be32(0) + be32(records.count) // no child: this node is a leaf
for entry in records {
    node += entry.data
}
guard node.count <= pageSize else {
    FileHandle.standardError.write(Data("the layout does not fit in one node (\(node.count) bytes)\n".utf8))
    exit(1)
}
node += Data(count: pageSize - node.count)

var tree = be32(2)          // the root node is block 2
tree += be32(0)             // levels below the root
tree += be32(records.count)
tree += be32(1)             // nodes
tree += be32(pageSize)
tree += Data(count: 32 - tree.count)

let blocks = [2048 | 11, 32 | 5, 4096 | 12] // the table, the tree record, the node
var table = be32(blocks.count) + be32(0)
for address in blocks {
    table += be32(address)
}
table += Data(count: (256 - blocks.count) * 4)
table += be32(1)                             // one named tree
table += Data([4]) + Data("DSDB".utf8) + be32(1) // "DSDB" is block 1
// What is left free, by size: 64 bytes at 64 up to 1 KiB at 1024, then
// everything past the node, 8 KiB at 8192 up to 1 GiB at 2^30.
for exponent in 0..<32 {
    let used = [5, 11, 12].contains(exponent) || exponent < 5 || exponent == 31
    if used {
        table += be32(0)
    } else {
        table += be32(1) + be32(1 << exponent)
    }
}
table += Data(count: 2048 - table.count)

var heap = Data("Bud1".utf8) + be32(2048) + be32(2048) + be32(2048) + Data(count: 16)
heap += tree
heap += Data(count: 2048 - heap.count)
heap += table
heap += node

let store = be32(1) + heap
try store.write(to: volume.appendingPathComponent(".DS_Store"))
print("laid out \(volume.path): \(records.count) records, \(store.count) bytes")
