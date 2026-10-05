import AppKit
import WhiteprintCore

public enum PageThumbnail {
    /// Width of the page the thumbnail shows, in points; content is laid out at
    /// this width and scaled down to the thumbnail.
    static let pageWidth: CGFloat = 480
    static let padding: CGFloat = 32
    static let fontSize: CGFloat = 15
    /// Text scaled below this many points is drawn as bars instead.
    static let minimumTextSize: CGFloat = 5

    /// A small vector-drawn preview of a page for the sidebar.
    public static func image(for page: NotePage, size: CGSize, palette: BlueprintPalette = .blueprint) -> NSImage {
        let image = NSImage(size: size, flipped: true) { rect in
            guard let context = NSGraphicsContext.current?.cgContext else { return false }
            draw(page, in: context, size: rect.size, palette: palette)
            return true
        }
        image.cacheMode = .never
        return image
    }

    /// The line without its heading marker and inline markup.
    static func plainText(_ line: String, markerLength: Int) -> String {
        let utf16 = Array(line.utf16)
        let body = Array(utf16[min(markerLength, utf16.count)...])
        let bodyString = String(utf16CodeUnits: body, count: body.count)
        var keep = Array(repeating: true, count: body.count)
        for span in MarkdownSyntax.spans(in: bodyString) {
            for markup in span.markupRanges {
                for i in markup.location..<NSMaxRange(markup) { keep[i] = false }
            }
        }
        let kept = body.indices.filter { keep[$0] }.map { body[$0] }
        return String(utf16CodeUnits: kept, count: kept.count)
    }

    /// Draws the preview into a y-down `context` with a current `NSGraphicsContext`.
    static func draw(_ page: NotePage, in context: CGContext, size: CGSize, palette: BlueprintPalette) {
        guard size.width > 0, size.height > 0 else { return }
        context.saveGState()
        context.clip(to: CGRect(origin: .zero, size: size))
        context.setFillColor(palette.pageBackground.cgColor)
        context.fill(CGRect(origin: .zero, size: size))
        let scale = size.width / pageWidth
        var spacing = SceneRenderer.unit * scale
        while spacing < 4 { spacing *= 2 }
        strokeGrid(in: context, size: size, spacing: spacing, color: palette.grid)

        context.scaleBy(x: scale, y: scale)
        var layout = ThumbnailLayout(
            context: context, palette: palette, scale: scale, bottom: size.height / scale - padding / 2
        )
        for block in page.blocks where !layout.isFull {
            switch block {
            case .text(let text): layout.addText(text)
            case .drawing(let drawing): layout.addDrawing(drawing.source)
            case .cards(let deck): layout.addText("🗂 " + (deck.title ?? "Flashcards") + " · \(deck.cards.count) cards")
            }
        }
        context.restoreGState()
    }

    /// The faint square grid, as hairlines `spacing` apart.
    private static func strokeGrid(in context: CGContext, size: CGSize, spacing: CGFloat, color: NSColor) {
        let path = CGMutablePath()
        var x = spacing
        while x < size.width {
            path.move(to: CGPoint(x: x, y: 0))
            path.addLine(to: CGPoint(x: x, y: size.height))
            x += spacing
        }
        var y = spacing
        while y < size.height {
            path.move(to: CGPoint(x: 0, y: y))
            path.addLine(to: CGPoint(x: size.width, y: y))
            y += spacing
        }
        context.saveGState()
        context.setStrokeColor(color.cgColor)
        context.setLineWidth(0.5)
        context.addPath(path)
        context.strokePath()
        context.restoreGState()
    }
}

/// Places a page's blocks top to bottom in page coordinates until it runs out of room.
private struct ThumbnailLayout {
    typealias T = PageThumbnail

    let context: CGContext
    let palette: BlueprintPalette
    let scale: CGFloat
    let bottom: CGFloat
    private var y = T.padding
    private let width = T.pageWidth - 2 * T.padding

    init(context: CGContext, palette: BlueprintPalette, scale: CGFloat, bottom: CGFloat) {
        self.context = context
        self.palette = palette
        self.scale = scale
        self.bottom = bottom
    }

    var isFull: Bool { y >= bottom }

    mutating func addText(_ text: String) {
        var fence: MarkdownFenceState?
        for line in text.split(separator: "\n", omittingEmptySubsequences: false) where !isFull {
            let classified = MarkdownSyntax.classify(String(line), openFence: fence)
            fence = classified.openFence
            addLine(String(line), as: classified.line)
        }
        y += T.fontSize * 0.6
    }

    mutating func addDrawing(_ source: String) {
        let scene = DrawingCompiler.compile(source).scene
        let canvas = SceneRenderer.canvasSize(for: scene)
        guard canvas.width > 2 * SceneRenderer.margin else { return }
        let fit = min(1, width / canvas.width)
        context.saveGState()
        context.translateBy(x: T.padding - SceneRenderer.margin * fit, y: y)
        context.scaleBy(x: fit, y: fit)
        SceneRenderer.draw(scene, in: context, palette: palette)
        context.restoreGState()
        y += canvas.height * fit + T.fontSize * 0.6
    }

    private mutating func addLine(_ line: String, as parsed: MarkdownLine) {
        var size = T.fontSize
        var color = palette.text
        var weight = NSFont.Weight.regular
        switch parsed.kind {
        case .blank:
            y += T.fontSize * 0.8
            return
        case .fence:
            return
        case .heading(let level):
            size = level == 1 ? 28 : level == 2 ? 22 : level == 3 ? 18 : T.fontSize
            weight = .semibold
        case .quote, .divider:
            color = palette.muted
        case .task(let checked):
            if checked { color = palette.muted }
        case .paragraph, .bullet, .ordered, .code:
            break
        }
        let display = T.plainText(line, markerLength: parsed.kind.isHeading ? parsed.markerLength : 0)
        let font = parsed.kind == .code
            ? NSFont.monospacedSystemFont(ofSize: size * 0.87, weight: .regular)
            : NSFont.systemFont(ofSize: size, weight: weight)
        let text = NSAttributedString(string: display, attributes: [.font: font, .foregroundColor: color])
        let lineHeight = ceil(size * 1.4)
        let textWidth = text.size().width
        let rows = max(1, min(3, Int(ceil(textWidth / width))))
        if size * scale >= T.minimumTextSize {
            text.draw(with: CGRect(x: T.padding, y: y, width: width, height: lineHeight * CGFloat(rows)),
                      options: [.usesLineFragmentOrigin, .truncatesLastVisibleLine])
        } else {
            drawBars(width: textWidth, rows: rows, size: size, lineHeight: lineHeight, color: color)
        }
        y += lineHeight * CGFloat(rows)
    }

    /// Rounded bars standing in for text too small to read.
    private func drawBars(width textWidth: CGFloat, rows: Int, size: CGFloat, lineHeight: CGFloat, color: NSColor) {
        let height = size * 0.55
        context.setFillColor(color.withAlphaComponent(color.alphaComponent * 0.6).cgColor)
        for row in 0..<rows {
            let remaining = textWidth - CGFloat(row) * width
            let barWidth = max(height, min(width, remaining))
            let rect = CGRect(x: T.padding, y: y + CGFloat(row) * lineHeight + (lineHeight - height) / 2, width: barWidth, height: height)
            context.addPath(CGPath(roundedRect: rect, cornerWidth: height / 2, cornerHeight: height / 2, transform: nil))
        }
        context.fillPath()
    }
}

private extension MarkdownLineKind {
    var isHeading: Bool {
        if case .heading = self { return true }
        return false
    }
}
