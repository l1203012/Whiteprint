import AppKit

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

public enum PDFExportStyle: String, CaseIterable {
    /// Blue pages, white text.
    case blueprint
    /// White paper, dark text.
    case print
}
