// Draws the Whiteprint app icon: a blueprint-blue rounded square with a white
// grid and a small three-box flow diagram, then packs it with iconutil.
//
//   swift Scripts/make-icon.swift      writes Resources/App/AppIcon.icns
import AppKit

let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
let iconset = FileManager.default.temporaryDirectory.appendingPathComponent("AppIcon.iconset", isDirectory: true)
let output = root.appendingPathComponent("Resources/App/AppIcon.icns")

func color(_ hex: UInt32, _ alpha: CGFloat = 1) -> NSColor {
    NSColor(
        srgbRed: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
        blue: CGFloat(hex & 0xFF) / 255, alpha: alpha
    )
}

/// Draws into a 1024-point canvas, y up.
func drawIcon() {
    // macOS icon grid: an 824-point squircle centred on the 1024 canvas.
    let tile = NSRect(x: 100, y: 100, width: 824, height: 824)
    let shape = NSBezierPath(roundedRect: tile, xRadius: 185, yRadius: 185)

    NSGraphicsContext.saveGraphicsState()
    let shadow = NSShadow()
    shadow.shadowColor = NSColor(white: 0, alpha: 0.3)
    shadow.shadowOffset = NSSize(width: 0, height: -10)
    shadow.shadowBlurRadius = 24
    shadow.set()
    color(0x1E4D8C).setFill()
    shape.fill()
    NSGraphicsContext.restoreGraphicsState()

    NSGraphicsContext.saveGraphicsState()
    shape.addClip()
    NSGradient(starting: color(0x2A62AA), ending: color(0x173E73))!.draw(in: tile, angle: -90)

    let step: CGFloat = 824 / 12
    for i in 1..<12 {
        let offset = CGFloat(i) * step
        let line = NSBezierPath()
        line.move(to: NSPoint(x: tile.minX + offset, y: tile.minY))
        line.line(to: NSPoint(x: tile.minX + offset, y: tile.maxY))
        line.move(to: NSPoint(x: tile.minX, y: tile.minY + offset))
        line.line(to: NSPoint(x: tile.maxX, y: tile.minY + offset))
        line.lineWidth = i % 3 == 0 ? 4 : 2
        NSColor(white: 1, alpha: i % 3 == 0 ? 0.16 : 0.08).setStroke()
        line.stroke()
    }
    NSGraphicsContext.restoreGraphicsState()

    // Three boxes joined by arrows, in white "ink", on grid lines.
    NSColor.white.setStroke()
    let boxes = [
        NSRect(x: tile.minX + step * 1.5, y: tile.minY + step * 7, width: step * 3, height: step * 2),
        NSRect(x: tile.minX + step * 7.5, y: tile.minY + step * 7, width: step * 3, height: step * 2),
        NSRect(x: tile.minX + step * 4.5, y: tile.minY + step * 2.5, width: step * 3, height: step * 2),
    ]
    for box in boxes {
        let path = NSBezierPath(roundedRect: box, xRadius: 14, yRadius: 14)
        path.lineWidth = 22
        path.stroke()
    }
    func arrow(_ points: NSPoint...) {
        let path = NSBezierPath()
        path.move(to: points[0])
        points.dropFirst().forEach(path.line(to:))
        let end = points[points.count - 1], before = points[points.count - 2]
        let angle = atan2(end.y - before.y, end.x - before.x)
        let head: CGFloat = 46
        for spread in [CGFloat.pi * 0.8, -CGFloat.pi * 0.8] {
            path.move(to: end)
            path.line(to: NSPoint(x: end.x + head * cos(angle + spread), y: end.y + head * sin(angle + spread)))
        }
        path.lineWidth = 20
        path.lineCapStyle = .round
        path.lineJoinStyle = .round
        path.stroke()
    }
    arrow(NSPoint(x: boxes[0].maxX + 24, y: boxes[0].midY), NSPoint(x: boxes[1].minX - 28, y: boxes[1].midY))
    arrow(
        NSPoint(x: boxes[1].midX, y: boxes[1].minY - 24), NSPoint(x: boxes[1].midX, y: boxes[2].midY),
        NSPoint(x: boxes[2].maxX + 28, y: boxes[2].midY)
    )
}

func png(pixels: Int) throws -> Data {
    guard let rep = NSBitmapImageRep(
        bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels, bitsPerSample: 8,
        samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
        bytesPerRow: 0, bitsPerPixel: 0
    ) else { throw CocoaError(.fileWriteUnknown) }
    rep.size = NSSize(width: pixels, height: pixels)
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    NSGraphicsContext.current?.imageInterpolation = .high
    let scale = CGFloat(pixels) / 1024
    let transform = NSAffineTransform()
    transform.scale(by: scale)
    transform.concat()
    drawIcon()
    NSGraphicsContext.restoreGraphicsState()
    guard let data = rep.representation(using: .png, properties: [:]) else { throw CocoaError(.fileWriteUnknown) }
    return data
}

try? FileManager.default.removeItem(at: iconset)
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
for points in [16, 32, 128, 256, 512] {
    try png(pixels: points).write(to: iconset.appendingPathComponent("icon_\(points)x\(points).png"))
    try png(pixels: points * 2).write(to: iconset.appendingPathComponent("icon_\(points)x\(points)@2x.png"))
}
try FileManager.default.createDirectory(at: output.deletingLastPathComponent(), withIntermediateDirectories: true)
let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = ["-c", "icns", iconset.path, "-o", output.path]
try iconutil.run()
iconutil.waitUntilExit()
guard iconutil.terminationStatus == 0 else { fatalError("iconutil failed") }
try? FileManager.default.removeItem(at: iconset)
print("✓ \(output.path)")
