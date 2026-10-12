// SPDX-License-Identifier: GPL-2.0-only
//
// Draws the Mac artwork from the same geometry as assets/talos.svg: the app
// icon (Talos.icns) and the picture behind the disk image's window. Run it
// when the mark changes; the results are committed, so a build needs
// neither this script nor a Swift toolchain.
//
//   swift packaging/macos/make-assets.swift packaging/macos
import AppKit

let out = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : ".")

let tile = NSColor(srgbRed: 0x1F / 255.0, green: 0x1D / 255.0, blue: 0x1A / 255.0, alpha: 1)
let tileTop = NSColor(srgbRed: 0x2E / 255.0, green: 0x2A / 255.0, blue: 0x25 / 255.0, alpha: 1)
let claw = NSColor(srgbRed: 0xE0 / 255.0, green: 0x7A / 255.0, blue: 0x45 / 255.0, alpha: 1)
let clawTop = NSColor(srgbRed: 0xF0 / 255.0, green: 0x96 / 255.0, blue: 0x5C / 255.0, alpha: 1)

/// The claw on the SVG's 64-unit grid, y downwards.
func clawPath() -> CGPath {
    let p = CGMutablePath()
    p.move(to: CGPoint(x: 17, y: 20))
    p.addCurve(to: CGPoint(x: 47, y: 36), control1: CGPoint(x: 30, y: 16), control2: CGPoint(x: 44, y: 22))
    p.addCurve(to: CGPoint(x: 40, y: 49), control1: CGPoint(x: 48, y: 42), control2: CGPoint(x: 45, y: 47))
    p.addCurve(to: CGPoint(x: 33, y: 32), control1: CGPoint(x: 43, y: 42), control2: CGPoint(x: 40, y: 35))
    p.addCurve(to: CGPoint(x: 27, y: 46), control1: CGPoint(x: 36, y: 38), control2: CGPoint(x: 33, y: 44))
    p.addCurve(to: CGPoint(x: 19, y: 31), control1: CGPoint(x: 30, y: 39), control2: CGPoint(x: 26, y: 33))
    p.addCurve(to: CGPoint(x: 17, y: 20), control1: CGPoint(x: 22, y: 28), control2: CGPoint(x: 22, y: 24))
    p.closeSubpath()
    return p
}

func bitmap(_ width: Int, _ height: Int, _ draw: (CGContext) -> Void) -> NSBitmapImageRep {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width, pixelsHigh: height,
                               bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                               colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    let context = NSGraphicsContext(bitmapImageRep: rep)!
    NSGraphicsContext.current = context
    draw(context.cgContext)
    context.flushGraphics()
    NSGraphicsContext.current = nil
    return rep
}

func gradient(_ a: NSColor, _ b: NSColor) -> CGGradient {
    CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: [a.cgColor, b.cgColor] as CFArray,
               locations: [0, 1])!
}

/// The icon at @p size pixels: Apple's grid for a 1024 canvas is an 824
/// tile with a soft shadow under it.
func icon(_ size: Int) -> NSBitmapImageRep {
    bitmap(size, size) { g in
        let s = CGFloat(size) / 1024
        g.scaleBy(x: s, y: s)
        let frame = CGRect(x: 100, y: 100, width: 824, height: 824)
        let shape = CGPath(roundedRect: frame, cornerWidth: 185, cornerHeight: 185, transform: nil)

        g.saveGState()
        g.setShadow(offset: CGSize(width: 0, height: -10 * s), blur: 20 * s,
                    color: NSColor.black.withAlphaComponent(0.35).cgColor)
        g.addPath(shape)
        g.setFillColor(tile.cgColor)
        g.fillPath()
        g.restoreGState()

        g.saveGState()
        g.addPath(shape)
        g.clip()
        g.drawLinearGradient(gradient(tileTop, tile), start: CGPoint(x: 512, y: 924),
                             end: CGPoint(x: 512, y: 100), options: [])
        g.restoreGState()

        // A hairline of light along the top edge, as on a physical tile.
        g.saveGState()
        g.addPath(shape)
        g.clip()
        g.addPath(CGPath(roundedRect: frame.insetBy(dx: 3, dy: 3), cornerWidth: 182, cornerHeight: 182,
                         transform: nil))
        g.setStrokeColor(NSColor.white.withAlphaComponent(0.08).cgColor)
        g.setLineWidth(6)
        g.strokePath()
        g.restoreGState()

        // The SVG's tile spans 2…62 of its 64 units; map that onto the frame.
        let unit = frame.width / 60
        var onto = CGAffineTransform(translationX: frame.minX - 2 * unit, y: frame.maxY + 2 * unit)
            .scaledBy(x: unit, y: -unit)
        let mark = clawPath().copy(using: &onto)!
        g.saveGState()
        g.setShadow(offset: CGSize(width: 0, height: -6 * s), blur: 18 * s,
                    color: NSColor.black.withAlphaComponent(0.35).cgColor)
        g.addPath(mark)
        g.setFillColor(claw.cgColor)
        g.fillPath()
        g.restoreGState()
        g.saveGState()
        g.addPath(mark)
        g.clip()
        g.drawLinearGradient(gradient(clawTop, claw), start: CGPoint(x: 512, y: 760),
                             end: CGPoint(x: 512, y: 300), options: [])
        g.restoreGState()
    }
}

/// The disk image's backdrop, @p scale pixels to the point. The window is
/// 660 by 400 points with the app at (170, 190) and Applications at
/// (490, 190); the arrow runs between them.
func backdrop(_ scale: Int) -> NSBitmapImageRep {
    bitmap(660 * scale, 400 * scale) { g in
        g.scaleBy(x: CGFloat(scale), y: CGFloat(scale))
        // Finder writes the names under the icons in black or in white,
        // after the system's appearance, so the ground is a mid tone that
        // carries both.
        let sand = NSColor(srgbRed: 0xE8 / 255.0, green: 0x8F / 255.0, blue: 0x5A / 255.0, alpha: 1)
        let ember = NSColor(srgbRed: 0xB8 / 255.0, green: 0x57 / 255.0, blue: 0x2C / 255.0, alpha: 1)
        g.drawLinearGradient(gradient(sand, ember), start: CGPoint(x: 0, y: 400),
                             end: CGPoint(x: 660, y: 0), options: [])
        // A soft pool of light behind where the icons sit.
        let glow = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB),
                              colors: [NSColor.white.withAlphaComponent(0.22).cgColor,
                                       NSColor.white.withAlphaComponent(0).cgColor] as CFArray,
                              locations: [0, 1])!
        g.drawRadialGradient(glow, startCenter: CGPoint(x: 330, y: 215), startRadius: 0,
                             endCenter: CGPoint(x: 330, y: 215), endRadius: 330, options: [])

        // The arrow from the app to the Applications folder.
        let ink = tile.withAlphaComponent(0.78).cgColor
        g.setStrokeColor(ink)
        g.setLineWidth(5)
        g.setLineCap(.round)
        g.setLineJoin(.round)
        g.move(to: CGPoint(x: 268, y: 214))
        g.addLine(to: CGPoint(x: 388, y: 214))
        g.strokePath()
        g.move(to: CGPoint(x: 370, y: 232))
        g.addLine(to: CGPoint(x: 390, y: 214))
        g.addLine(to: CGPoint(x: 370, y: 196))
        g.strokePath()

        let centred = NSMutableParagraphStyle()
        centred.alignment = .center
        let title = NSAttributedString(string: "Talos", attributes: [
            .font: NSFont.systemFont(ofSize: 30, weight: .semibold),
            .foregroundColor: tile, .paragraphStyle: centred, .kern: 0.4,
        ])
        title.draw(in: CGRect(x: 0, y: 326, width: 660, height: 40))
        let hint = NSAttributedString(string: "Drag the app onto the Applications folder to install it", attributes: [
            .font: NSFont.systemFont(ofSize: 13, weight: .medium),
            .foregroundColor: tile.withAlphaComponent(0.8), .paragraphStyle: centred,
        ])
        hint.draw(in: CGRect(x: 0, y: 40, width: 660, height: 20))
    }
}

func write(_ rep: NSBitmapImageRep, _ type: NSBitmapImageRep.FileType, _ url: URL) throws {
    try rep.representation(using: type, properties: [:])!.write(to: url)
}

let scratch = FileManager.default.temporaryDirectory.appendingPathComponent("talos-assets-\(getpid())")
let iconset = scratch.appendingPathComponent("Talos.iconset")
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
defer { try? FileManager.default.removeItem(at: scratch) }

for points in [16, 32, 128, 256, 512] {
    try write(icon(points), .png, iconset.appendingPathComponent("icon_\(points)x\(points).png"))
    try write(icon(points * 2), .png, iconset.appendingPathComponent("icon_\(points)x\(points)@2x.png"))
}

func run(_ tool: String, _ arguments: [String]) throws {
    let process = Process()
    process.executableURL = URL(fileURLWithPath: tool)
    process.arguments = arguments
    try process.run()
    process.waitUntilExit()
    if process.terminationStatus != 0 {
        throw NSError(domain: tool, code: Int(process.terminationStatus))
    }
}

try run("/usr/bin/iconutil", ["-c", "icns", iconset.path, "-o", out.appendingPathComponent("Talos.icns").path])

// One TIFF holding both sizes, so the picture is sharp on any display.
let small = scratch.appendingPathComponent("backdrop.png")
let large = scratch.appendingPathComponent("backdrop@2x.png")
try write(backdrop(1), .png, small)
try write(backdrop(2), .png, large)
try run("/usr/bin/tiffutil", ["-cathidpicheck", small.path, large.path, "-out",
                              out.appendingPathComponent("dmg-background.tiff").path])
print("wrote Talos.icns and dmg-background.tiff to \(out.path)")
