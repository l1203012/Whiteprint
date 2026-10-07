import AppKit

/// Page colours. On screen the palette comes from the chosen
/// `PageTheme`; `print` is the white-paper variant for PDF export.
public struct BlueprintPalette {
    public var pageBackground: NSColor
    public var grid: NSColor
    public var text: NSColor
    public var muted: NSColor
    public var accent: NSColor
    /// Behind the pages, around them.
    public var canvas: NSColor
    /// Whether pages draw the blueprint grid.
    public var showsGrid: Bool
    /// The hairline around each page.
    public var pageEdge: NSColor
    /// The opacity of the page's drop shadow.
    public var shadowOpacity: CGFloat
    /// Whether each page is drawn as a sheet with an edge and shadow. Without
    /// one, text sits straight on the canvas and pages are split by a short rule.
    public var drawsSheet: Bool
    /// The fill of a ticked checkbox.
    public var checkbox: NSColor

    public init(
        pageBackground: NSColor, grid: NSColor, text: NSColor, muted: NSColor, accent: NSColor,
        canvas: NSColor = .windowBackgroundColor, showsGrid: Bool = true,
        pageEdge: NSColor = NSColor(white: 1, alpha: 0.08), shadowOpacity: CGFloat = 0.22,
        drawsSheet: Bool = true, checkbox: NSColor? = nil
    ) {
        self.pageBackground = pageBackground
        self.grid = grid
        self.text = text
        self.muted = muted
        self.accent = accent
        self.canvas = canvas
        self.showsGrid = showsGrid
        self.pageEdge = pageEdge
        self.shadowOpacity = shadowOpacity
        self.drawsSheet = drawsSheet
        self.checkbox = checkbox ?? accent
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

    /// Warm off-white paper and near-black text, or charcoal and soft white in
    /// dark mode; no sheet.
    public static let paper = BlueprintPalette(
        pageBackground: adaptive(rgb(0xFCFBF9), dark: rgb(0x1E1E20)),
        grid: .clear,
        text: adaptive(rgb(0x1D1D1F), dark: rgb(0xECECEE)),
        muted: adaptive(rgb(0x86868B), dark: rgb(0x98989D)),
        accent: adaptive(rgb(0x0A66D8), dark: rgb(0x4C9AFF)),
        canvas: adaptive(rgb(0xFCFBF9), dark: rgb(0x1E1E20)),
        showsGrid: false,
        pageEdge: adaptive(NSColor(white: 0, alpha: 0.09), dark: NSColor(white: 1, alpha: 0.12)),
        shadowOpacity: 0,
        drawsSheet: false
    )

    /// Cream paper, brown ink.
    public static let sepia = BlueprintPalette(
        pageBackground: rgb(0xF8F1E3),
        grid: .clear,
        text: rgb(0x3A3026),
        muted: rgb(0x8A7A66),
        accent: rgb(0x9C5B2E),
        canvas: rgb(0xF8F1E3),
        showsGrid: false,
        pageEdge: NSColor(white: 0, alpha: 0.1),
        shadowOpacity: 0,
        drawsSheet: false
    )

    /// Charcoal pages, soft white text.
    public static let night = BlueprintPalette(
        pageBackground: rgb(0x1F1F22),
        grid: .clear,
        text: rgb(0xE8E8EA),
        muted: rgb(0x8E8E93),
        accent: rgb(0x6CA8FF),
        canvas: rgb(0x1F1F22),
        showsGrid: false,
        pageEdge: NSColor(white: 1, alpha: 0.12),
        shadowOpacity: 0,
        drawsSheet: false
    )

    /// `light`, or `dark` under a dark appearance.
    private static func adaptive(_ light: NSColor, dark: NSColor) -> NSColor {
        NSColor(name: nil) { appearance in
            appearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua ? dark : light
        }
    }

    private static func rgb(_ hex: UInt32) -> NSColor {
        NSColor(srgbRed: CGFloat(hex >> 16 & 0xFF) / 255, green: CGFloat(hex >> 8 & 0xFF) / 255,
                blue: CGFloat(hex & 0xFF) / 255, alpha: 1)
    }
}

/// The look of pages on screen, chosen in View ▸ Page Theme or Settings.
public enum PageTheme: String, CaseIterable {
    case paper
    case sepia
    case blueprint
    case night

    public static let `default` = PageTheme.paper

    public var title: String {
        switch self {
        case .paper: return "Paper"
        case .sepia: return "Sepia"
        case .blueprint: return "Blueprint"
        case .night: return "Night"
        }
    }

    public var palette: BlueprintPalette {
        switch self {
        case .paper: return .paper
        case .sepia: return .sepia
        case .blueprint: return .blueprint
        case .night: return .night
        }
    }
}

public enum PDFExportStyle: String, CaseIterable {
    /// Blue pages, white text.
    case blueprint
    /// White paper, dark text.
    case print
}
