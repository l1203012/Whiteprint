// Draws the background of the install DMG window on a blueprint sheet:
// "Drag Whiteprint to Applications" with an arrow between the two icon spots.
// Writes Resources/DMG/background.png (1x) and background@2x.png. It uses
// WhiteprintRender, so compile it against the module:
//
//   Scripts/build.sh WhiteprintRender
//   D=.build/clt/debug/$(uname -m)
//   swiftc -I $D -L $D -lWhiteprintRender -lWhiteprintCore Scripts/make-dmg-background.swift -o /tmp/make-dmg-background
//   /tmp/make-dmg-background
//
// Keep the window size and icon spots in sync with Scripts/release.sh.
import AppKit
import WhiteprintRender

let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
let output = root.appendingPathComponent("Resources/DMG", isDirectory: true)

/// The window's content size in points.
let window = CGSize(width: 640, height: 400)
/// Icon centres, y down, as Finder positions them.
let appSpot = CGPoint(x: 160, y: 205)
let applicationsSpot = CGPoint(x: 480, y: 205)

let palette = BlueprintPalette.blueprint

func centered(_ string: String, font: NSFont, color: NSColor, kern: CGFloat = 0, y: CGFloat) {
    let text = NSAttributedString(string: string, attributes: [.font: font, .foregroundColor: color, .kern: kern])
    let size = text.size()
    text.draw(at: CGPoint(x: (window.width - size.width) / 2, y: y))
}

/// A translucent plate with a dashed outline around an icon and its label,
/// light enough for Finder's black labels and dark enough for its white ones.
func drawSpot(at center: CGPoint, in context: CGContext) {
    let plate = CGRect(x: center.x - 80, y: center.y - 78, width: 160, height: 180)
    let path = CGPath(roundedRect: plate, cornerWidth: 14, cornerHeight: 14, transform: nil)
    context.setFillColor(NSColor(white: 1, alpha: 0.16).cgColor)
    context.addPath(path)
    context.fillPath()
    context.setStrokeColor(NSColor(white: 1, alpha: 0.75).cgColor)
    context.setLineWidth(1)
    context.setLineDash(phase: 0, lengths: [5, 4])
    context.addPath(path)
    context.strokePath()
    context.setLineDash(phase: 0, lengths: [])
}

/// A gently arched arrow from the app spot to the Applications spot.
func drawArrow(in context: CGContext) {
    let start = CGPoint(x: appSpot.x + 100, y: appSpot.y)
    let end = CGPoint(x: applicationsSpot.x - 104, y: applicationsSpot.y)
    let control = CGPoint(x: (start.x + end.x) / 2, y: start.y - 34)
    context.setStrokeColor(NSColor.white.cgColor)
    context.setLineWidth(2.5)
    context.setLineCap(.round)
    context.setLineDash(phase: 0, lengths: [7, 6])
    context.move(to: start)
    context.addQuadCurve(to: end, control: control)
    context.strokePath()
    context.setLineDash(phase: 0, lengths: [])

    // Arrowhead along the curve's end tangent.
    let angle = atan2(end.y - control.y, end.x - control.x)
    let length: CGFloat = 14, spread: CGFloat = 0.45
    context.setFillColor(NSColor.white.cgColor)
    context.move(to: CGPoint(x: end.x + 4 * cos(angle), y: end.y + 4 * sin(angle)))
    context.addLine(to: CGPoint(x: end.x - length * cos(angle - spread), y: end.y - length * sin(angle - spread)))
    context.addLine(to: CGPoint(x: end.x - length * cos(angle + spread), y: end.y - length * sin(angle + spread)))
    context.closePath()
    context.fillPath()
}

/// Draws the background into a y-down context of `window` points.
func draw(in context: CGContext) {
    let sheet = CGRect(origin: .zero, size: window)
    BlueprintBackground.draw(in: context, rect: sheet, palette: palette, options: .init(frame: true))
    drawSpot(at: appSpot, in: context)
    drawSpot(at: applicationsSpot, in: context)
    drawArrow(in: context)
    centered("Drag Whiteprint to Applications", font: .systemFont(ofSize: 22, weight: .semibold), color: palette.text, y: 48)
    centered("THEN OPEN IT FROM YOUR APPLICATIONS FOLDER", font: .systemFont(ofSize: 9, weight: .medium),
             color: palette.muted, kern: 1.6, y: 340)
}

func write(scale: Int, to name: String) throws {
    guard let rep = NSBitmapImageRep(
        bitmapDataPlanes: nil, pixelsWide: Int(window.width) * scale, pixelsHigh: Int(window.height) * scale,
        bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
        colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0
    ), let graphics = NSGraphicsContext(bitmapImageRep: rep)
    else { throw CocoaError(.fileWriteUnknown) }
    rep.size = window
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(cgContext: graphics.cgContext, flipped: true)
    let context = graphics.cgContext
    context.scaleBy(x: CGFloat(scale), y: CGFloat(scale))
    context.translateBy(x: 0, y: window.height)
    context.scaleBy(x: 1, y: -1)
    draw(in: context)
    NSGraphicsContext.restoreGraphicsState()
    guard let png = rep.representation(using: .png, properties: [:]) else { throw CocoaError(.fileWriteUnknown) }
    try png.write(to: output.appendingPathComponent(name))
    print("✓ Resources/DMG/\(name)")
}

try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
try write(scale: 1, to: "background.png")
try write(scale: 2, to: "background@2x.png")
