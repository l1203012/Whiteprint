import AppKit

/// An sRGB, y-down offscreen canvas at 1 px per point, for sampling rendered pixels.
final class Bitmap {
    let width: Int
    let height: Int
    let context: CGContext

    init(width: Int, height: Int, background: NSColor? = nil) {
        self.width = width
        self.height = height
        context = CGContext(
            data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
            space: CGColorSpace(name: CGColorSpace.sRGB)!,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        )!
        context.translateBy(x: 0, y: CGFloat(height))
        context.scaleBy(x: 1, y: -1)
        if let background {
            context.setFillColor(background.cgColor)
            context.fill(CGRect(x: 0, y: 0, width: width, height: height))
        }
    }

    /// Runs `body` with a flipped `NSGraphicsContext` on this bitmap made current.
    func withGraphicsContext(_ body: () -> Void) {
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: context, flipped: true)
        body()
        NSGraphicsContext.restoreGraphicsState()
    }

    /// The pixel at (x, y) in y-down coordinates, as 0–255 RGBA.
    func pixel(_ x: Int, _ y: Int) -> (r: Int, g: Int, b: Int, a: Int) {
        let data = context.data!.assumingMemoryBound(to: UInt8.self)
        let offset = y * context.bytesPerRow + x * 4
        return (Int(data[offset]), Int(data[offset + 1]), Int(data[offset + 2]), Int(data[offset + 3]))
    }

    /// Whether the pixel is close to `color` (within `tolerance` per channel).
    func pixel(_ x: Int, _ y: Int, matches color: NSColor, tolerance: Int = 12) -> Bool {
        let p = pixel(x, y)
        let c = color.usingColorSpace(.sRGB)!
        let expected = [c.redComponent, c.greenComponent, c.blueComponent].map { Int(($0 * 255).rounded()) }
        return abs(p.r - expected[0]) <= tolerance && abs(p.g - expected[1]) <= tolerance && abs(p.b - expected[2]) <= tolerance
    }

    /// How many pixels in the bitmap are close to `color`.
    func count(matching color: NSColor, tolerance: Int = 40) -> Int {
        var count = 0
        for y in 0..<height {
            for x in 0..<width where pixel(x, y, matches: color, tolerance: tolerance) {
                count += 1
            }
        }
        return count
    }
}
