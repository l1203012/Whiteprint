import AppKit
import WhiteprintCore

// CONTRACT (owner: render agent). Public signatures are fixed; bodies are stubs.

/// Page colours. The blueprint palette is used on screen in both light and
/// dark mode; `print` is the white-paper variant for PDF export.
public struct BlueprintPalette {
    public var pageBackground: NSColor
    public var grid: NSColor
    public var text: NSColor
    public var muted: NSColor
    public var accent: NSColor

    public init(pageBackground: NSColor, grid: NSColor, text: NSColor, muted: NSColor, accent: NSColor) {
        self.pageBackground = pageBackground
        self.grid = grid
        self.text = text
        self.muted = muted
        self.accent = accent
    }

    /// `#1E4D8C` page, white at 6% grid, white text, white at 65% muted, `#9FD3FF` accent.
    public static let blueprint = BlueprintPalette(
        pageBackground: NSColor(srgbRed: 0x1E / 255, green: 0x4D / 255, blue: 0x8C / 255, alpha: 1),
        grid: NSColor(white: 1, alpha: 0.06),
        text: .white,
        muted: NSColor(white: 1, alpha: 0.65),
        accent: NSColor(srgbRed: 0x9F / 255, green: 0xD3 / 255, blue: 0xFF / 255, alpha: 1)
    )

    public static let print = BlueprintPalette(
        pageBackground: .white,
        grid: NSColor(white: 0, alpha: 0.05),
        text: NSColor(white: 0.1, alpha: 1),
        muted: NSColor(white: 0.4, alpha: 1),
        accent: NSColor(srgbRed: 0x1E / 255, green: 0x4D / 255, blue: 0x8C / 255, alpha: 1)
    )
}

public enum SceneRenderer {
    /// Points per grid unit.
    public static let unit: CGFloat = 10
    /// Space around the scene's bounds, in points.
    public static let margin: CGFloat = 10

    /// Size of the canvas `draw` fills: the scene's bounds plus margins.
    public static func canvasSize(for scene: DrawingScene) -> CGSize {
        .zero
    }

    /// Draws the scene with its bounds' top-left at (margin, margin).
    /// `context` must be y-down (flipped), as in a flipped NSView.
    public static func draw(_ scene: DrawingScene, in context: CGContext, palette: BlueprintPalette) {}
}

/// Renders drawing-language source. Flipped, sized to its content, transparent
/// background so the page grid shows through. Recompiles when `source` changes.
public final class DrawingView: NSView {
    public var source: String {
        didSet { needsDisplay = true }
    }
    public var palette: BlueprintPalette
    /// Errors from the last compile.
    public private(set) var errors: [DrawingError] = []

    public init(source: String, palette: BlueprintPalette = .blueprint) {
        self.source = source
        self.palette = palette
        super.init(frame: .zero)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    public override var isFlipped: Bool { true }
}

/// Markdown look shared by the editor and PDF export: headings, bold/italic,
/// inline code, code blocks, lists, checklists, quotes, links. The markup
/// characters stay in the text (editing is plain Markdown) but are muted.
public enum MarkdownStyler {
    public static let defaultFontSize: CGFloat = 15

    /// Restyles `storage` (or just the paragraphs touching `range`).
    public static func apply(
        to storage: NSTextStorage, in range: NSRange? = nil,
        palette: BlueprintPalette, fontSize: CGFloat = defaultFontSize
    ) {}

    /// A styled copy of `markdown`, for read-only display and export.
    public static func attributedString(
        markdown: String, palette: BlueprintPalette, fontSize: CGFloat = defaultFontSize
    ) -> NSAttributedString {
        NSAttributedString(string: markdown)
    }
}

public enum PDFExportStyle: String, CaseIterable {
    /// Blue pages, white text.
    case blueprint
    /// White paper, dark text.
    case print
}

public enum PDFExporter {
    /// A4 or US Letter by locale, typeset text only (drawings are left out),
    /// one or more PDF pages per note page, page breaks at `+++page`.
    public static func data(for note: Note, style: PDFExportStyle) -> Data {
        Data()
    }
}

public enum PageThumbnail {
    /// A small vector-drawn preview of a page for the sidebar.
    public static func image(for page: NotePage, size: CGSize, palette: BlueprintPalette = .blueprint) -> NSImage {
        NSImage(size: size)
    }
}
