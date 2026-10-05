import AppKit
import CoreText

/// A blueprint sheet: the page colour with a soft vignette gradient, a
/// fine paper-fibre texture, a 10 pt / 50 pt grid, and optionally a drawing
/// frame with a title block in the bottom-right corner.
///
/// The grid and the texture are tiling patterns, so in a PDF the sheet stays
/// small and does not break up the copied text of whatever is drawn on top.
public enum BlueprintBackground {
    /// Points between minor grid lines.
    public static let minorSpacing: CGFloat = 10
    /// Points between major grid lines.
    public static let majorSpacing: CGFloat = 50
    /// Distance from the sheet's edge to the drawing frame.
    public static let frameInset: CGFloat = 20
    /// Size of the title block in the frame's bottom-right corner.
    public static let titleBlockSize = CGSize(width: 290, height: 30)

    public struct Options {
        /// Lighter towards the centre, deeper towards the edges.
        public var gradient: Bool
        public var texture: Bool
        public var grid: Bool
        /// A thin rule `frameInset` inside the edge; the grid stays inside it.
        public var frame: Bool
        /// Drawn in the frame's bottom-right corner; implies `frame`. In a PDF,
        /// leave it out and call `drawTitleBlock` after the page's text, so
        /// copied text starts with the page's own.
        public var titleBlock: TitleBlock?

        public init(gradient: Bool = true, texture: Bool = true, grid: Bool = true, frame: Bool = false, titleBlock: TitleBlock? = nil) {
            self.gradient = gradient
            self.texture = texture
            self.grid = grid
            self.frame = frame
            self.titleBlock = titleBlock
        }

        /// The page colour alone.
        public static let plain = Options(gradient: false, texture: false, grid: false)
    }

    public struct TitleBlock {
        public var title: String
        public var page: Int
        public var pageCount: Int
        public var date: Date

        public init(title: String, page: Int, pageCount: Int, date: Date) {
            self.title = title
            self.page = page
            self.pageCount = pageCount
            self.date = date
        }
    }

    /// Fills `rect` of a y-down (flipped) `context` with the sheet.
    public static func draw(in context: CGContext, rect: CGRect, palette: BlueprintPalette, options: Options = Options()) {
        guard rect.width > 0, rect.height > 0 else { return }
        context.saveGState()
        context.clip(to: rect)
        context.setFillColor(palette.pageBackground.cgColor)
        context.fill(rect)
        if options.gradient { drawGradient(in: context, rect: rect, base: palette.pageBackground) }
        if options.texture { drawTexture(in: context, rect: rect) }
        let framed = options.frame || options.titleBlock != nil
        let inner = framed ? rect.insetBy(dx: frameInset, dy: frameInset) : rect
        if options.grid { drawGrid(in: context, rect: inner, palette: palette) }
        if framed { drawFrame(in: context, rect: inner, palette: palette) }
        context.restoreGState()
        if let block = options.titleBlock { drawTitleBlock(block, in: context, rect: rect, palette: palette) }
    }

    // MARK: - Paper

    /// One opaque radial gradient around the page colour: lit a little above
    /// and left of the centre, deepening towards the edges like a vignette.
    private static func drawGradient(in context: CGContext, rect: CGRect, base: NSColor) {
        guard let space = CGColorSpace(name: CGColorSpace.sRGB) else { return }
        let lit = base.blended(withFraction: 0.08, of: .white) ?? base
        let deep = base.blended(withFraction: 0.3, of: NSColor(srgbRed: 0, green: 0.03, blue: 0.12, alpha: 1)) ?? base
        let colors = [lit.cgColor, base.cgColor, deep.cgColor] as CFArray
        guard let gradient = CGGradient(colorsSpace: space, colors: colors, locations: [0, 0.5, 1]) else { return }
        let center = CGPoint(x: rect.minX + rect.width * 0.42, y: rect.minY + rect.height * 0.38)
        let reach = hypot(max(center.x - rect.minX, rect.maxX - center.x), max(center.y - rect.minY, rect.maxY - center.y))
        context.drawRadialGradient(gradient, startCenter: center, startRadius: 0, endCenter: center, endRadius: reach, options: [.drawsAfterEndLocation])
    }

    private static func drawTexture(in context: CGContext, rect: CGRect) {
        guard let tile = PaperTexture.tile else { return }
        context.saveGState()
        context.translateBy(x: rect.minX, y: rect.minY)
        context.draw(tile, in: CGRect(x: 0, y: 0, width: PaperTexture.points, height: PaperTexture.points), byTiling: true)
        context.restoreGState()
    }

    // MARK: - Grid and frame

    /// Minor and major lines as one colored tiling pattern anchored at `rect`'s
    /// top-left. In a PDF, hundreds of stroked lines make viewers split copied
    /// text into fragments; a pattern does not.
    private static func drawGrid(in context: CGContext, rect: CGRect, palette: BlueprintPalette) {
        let grid = palette.grid
        let tile = GridTile(minor: grid.cgColor, major: grid.withAlphaComponent(min(1, grid.alphaComponent * 2.4)).cgColor)
        var callbacks = CGPatternCallbacks(version: 0, drawPattern: { info, context in
            guard let info else { return }
            Unmanaged<GridTile>.fromOpaque(info).takeUnretainedValue().draw(in: context)
        }, releaseInfo: { info in
            guard let info else { return }
            Unmanaged<GridTile>.fromOpaque(info).release()
        })
        // Pattern space maps to the context's base space, not the current CTM.
        let matrix = CGAffineTransform(translationX: rect.minX, y: rect.minY).concatenating(context.ctm)
        guard let space = CGColorSpace(patternBaseSpace: nil),
              let pattern = CGPattern(
                  info: Unmanaged.passRetained(tile).toOpaque(),
                  bounds: CGRect(x: 0, y: 0, width: majorSpacing, height: majorSpacing),
                  matrix: matrix, xStep: majorSpacing, yStep: majorSpacing,
                  tiling: .constantSpacing, isColored: true, callbacks: &callbacks
              )
        else { return }
        var alpha: CGFloat = 1
        context.saveGState()
        context.setFillColorSpace(space)
        context.setFillPattern(pattern, colorComponents: &alpha)
        context.fill(rect)
        context.restoreGState()
    }

    private static func ruleColor(_ palette: BlueprintPalette) -> CGColor {
        palette.text.withAlphaComponent(palette.text.alphaComponent * 0.7).cgColor
    }

    private static func drawFrame(in context: CGContext, rect: CGRect, palette: BlueprintPalette) {
        context.setStrokeColor(ruleColor(palette))
        context.setLineWidth(0.75)
        context.stroke(rect)
    }

    // MARK: - Title block

    private static let dateFormat: DateFormatter = {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd"
        return formatter
    }()

    /// Draws the title block of a sheet filling `rect` of a y-down `context`:
    /// title │ date │ sheet │ WHITEPRINT, each cell a small caption over its value.
    public static func drawTitleBlock(_ block: TitleBlock, in context: CGContext, rect: CGRect, palette: BlueprintPalette) {
        let frame = rect.insetBy(dx: frameInset, dy: frameInset)
        let size = titleBlockSize
        let box = CGRect(x: frame.maxX - size.width, y: frame.maxY - size.height, width: size.width, height: size.height)
        let widths: [CGFloat] = [118, 62, 42, 68]
        var edges = [box.minX]
        for width in widths { edges.append(edges[edges.count - 1] + width) }

        context.saveGState()
        context.setFillColor(palette.pageBackground.cgColor)
        context.fill(box)
        context.setStrokeColor(ruleColor(palette))
        context.setLineWidth(0.5)
        context.stroke(box)
        for x in edges.dropFirst().dropLast() {
            context.strokeLineSegments(between: [CGPoint(x: x, y: box.minY), CGPoint(x: x, y: box.maxY)])
        }

        let caption = NSFont.systemFont(ofSize: 5, weight: .medium)
        let value = NSFont.systemFont(ofSize: 8, weight: .semibold)
        let mark = NSFont.systemFont(ofSize: 7, weight: .heavy)
        let muted = palette.muted.cgColor
        let ink = palette.text.cgColor
        let cells: [(caption: String, value: String)] = [
            ("TITLE", block.title),
            ("DATE", dateFormat.string(from: block.date)),
            ("SHEET", "\(block.page) / \(block.pageCount)"),
        ]
        let pad: CGFloat = 5
        for (i, cell) in cells.enumerated() {
            let width = widths[i] - 2 * pad
            TitleText.draw(cell.caption, font: caption, kern: 0.8, color: muted, width: width,
                           at: CGPoint(x: edges[i] + pad, y: box.minY + 9), in: context)
            TitleText.draw(cell.value, font: value, kern: 0, color: ink, width: width,
                           at: CGPoint(x: edges[i] + pad, y: box.maxY - 7), in: context)
        }
        let markWidth = TitleText.width("WHITEPRINT", font: mark, kern: 1) - 1
        let markCell = CGRect(x: edges[3], y: box.minY, width: widths[3], height: box.height)
        TitleText.draw("WHITEPRINT", font: mark, kern: 1, color: ink, width: markCell.width,
                       at: CGPoint(x: markCell.midX - markWidth / 2, y: markCell.midY + 2.6), in: context)
        context.restoreGState()
    }
}

/// The two line weights of one 50 pt grid cell.
private final class GridTile {
    static let minorWidth: CGFloat = 0.3
    static let majorWidth: CGFloat = 0.6

    let minor: CGColor
    let major: CGColor

    init(minor: CGColor, major: CGColor) {
        self.minor = minor
        self.major = major
    }

    func draw(in context: CGContext) {
        let spacing = BlueprintBackground.minorSpacing
        let size = BlueprintBackground.majorSpacing
        context.setFillColor(minor)
        var offset = spacing
        while offset < size {
            context.fill(CGRect(x: offset, y: 0, width: Self.minorWidth, height: size))
            context.fill(CGRect(x: 0, y: offset, width: size, height: Self.minorWidth))
            offset += spacing
        }
        context.setFillColor(major)
        context.fill(CGRect(x: 0, y: 0, width: Self.majorWidth, height: size))
        context.fill(CGRect(x: Self.majorWidth, y: 0, width: size - Self.majorWidth, height: Self.majorWidth))
    }
}

/// A small, seamless tile of paper fibres and speckle: white through an
/// 8-bit mask generated from a fixed seed. In a PDF only the mask costs bytes.
enum PaperTexture {
    /// Tile size in points; the image has two pixels per point.
    static let points: CGFloat = 96
    static let pixels = 192

    static let tile: CGImage? = make(seed: 0x5EED_B1E9)

    static func make(seed: UInt64) -> CGImage? {
        let size = pixels
        guard let coverage = coverage(seed: seed, size: size),
              let maskProvider = CGDataProvider(data: coverage as CFData),
              let mask = CGImage(
                  maskWidth: size, height: size, bitsPerComponent: 8, bitsPerPixel: 8, bytesPerRow: size,
                  provider: maskProvider, decode: nil, shouldInterpolate: true
              ),
              let whiteProvider = CGDataProvider(data: Data(repeating: 255, count: size * size) as CFData),
              let white = CGImage(
                  width: size, height: size, bitsPerComponent: 8, bitsPerPixel: 8, bytesPerRow: size,
                  space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGBitmapInfo(rawValue: 0),
                  provider: whiteProvider, decode: nil, shouldInterpolate: true, intent: .defaultIntent
              )
        else { return nil }
        return white.masking(mask)
    }

    /// Mask samples: 255 leaves the paper bare, lower values let white through.
    static func coverage(seed: UInt64, size: Int) -> Data? {
        guard let context = CGContext(
            data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: size,
            space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGImageAlphaInfo.none.rawValue
        ) else { return nil }
        var random = SplitMix64(seed: seed)
        let extent = CGFloat(size)
        context.setFillColor(gray: 1, alpha: 1)
        context.fill(CGRect(x: 0, y: 0, width: extent, height: extent))
        context.setLineCap(.round)

        // Fibres: short, gently curved strands, drawn at each wrap-around offset
        // so the tile is seamless.
        for _ in 0..<140 {
            let start = CGPoint(x: random.unit * extent, y: random.unit * extent)
            let angle = random.unit * 2 * .pi
            let length = 8 + random.unit * 30
            let bend = (random.unit - 0.5) * length * 0.35
            let end = CGPoint(x: start.x + cos(angle) * length, y: start.y + sin(angle) * length)
            let control = CGPoint(x: (start.x + end.x) / 2 - sin(angle) * bend, y: (start.y + end.y) / 2 + cos(angle) * bend)
            context.setStrokeColor(gray: 0, alpha: 0.02 + random.unit * 0.035)
            context.setLineWidth(0.6 + random.unit * 0.9)
            for dx in [-extent, 0, extent] {
                for dy in [-extent, 0, extent] {
                    context.move(to: CGPoint(x: start.x + dx, y: start.y + dy))
                    context.addQuadCurve(to: CGPoint(x: end.x + dx, y: end.y + dy), control: CGPoint(x: control.x + dx, y: control.y + dy))
                }
            }
            context.strokePath()
        }

        // Speckle: sparse single pixels in a few strengths, so the tile compresses.
        guard let pixels = context.data?.assumingMemoryBound(to: UInt8.self) else { return nil }
        for _ in 0..<(size * size / 10) {
            let offset = Int(random.next() % UInt64(size * size))
            pixels[offset] = min(pixels[offset], UInt8(249 - 5 * (random.next() % 3)))
        }
        return Data(bytes: pixels, count: size * size)
    }
}

/// A small deterministic pseudo-random generator.
struct SplitMix64 {
    private var state: UInt64

    init(seed: UInt64) {
        state = seed
    }

    mutating func next() -> UInt64 {
        state &+= 0x9E37_79B9_7F4A_7C15
        var z = state
        z = (z ^ (z >> 30)) &* 0xBF58_476D_1CE4_E5B9
        z = (z ^ (z >> 27)) &* 0x94D0_49BB_1331_11EB
        return z ^ (z >> 31)
    }

    /// A value in 0..<1.
    var unit: CGFloat {
        mutating get { CGFloat(next() >> 11) / CGFloat(1 << 53) }
    }
}

/// Single lines of title block text.
private enum TitleText {
    static func width(_ string: String, font: NSFont, kern: CGFloat) -> CGFloat {
        CGFloat(CTLineGetTypographicBounds(line(string, font: font, kern: kern), nil, nil, nil))
    }

    /// Draws `string` with its baseline starting at `origin` in a y-down
    /// `context`, truncated with an ellipsis to `width`.
    static func draw(_ string: String, font: NSFont, kern: CGFloat, color: CGColor, width: CGFloat, at origin: CGPoint, in context: CGContext) {
        var text = line(string, font: font, kern: kern)
        if CGFloat(CTLineGetTypographicBounds(text, nil, nil, nil)) > width,
           let truncated = CTLineCreateTruncatedLine(text, Double(width), .end, line("…", font: font, kern: kern)) {
            text = truncated
        }
        context.saveGState()
        context.setFillColor(color)
        context.textMatrix = CGAffineTransform(scaleX: 1, y: -1)
        context.textPosition = origin
        CTLineDraw(text, context)
        context.restoreGState()
    }

    private static func line(_ string: String, font: NSFont, kern: CGFloat) -> CTLine {
        let attributes: [NSAttributedString.Key: Any] = [
            .font: font, .kern: kern,
            NSAttributedString.Key(kCTForegroundColorFromContextAttributeName as String): true,
        ]
        return CTLineCreateWithAttributedString(NSAttributedString(string: string, attributes: attributes))
    }
}
