import CoreGraphics
import Foundation

/// Where pages and their text column go for a given document width: a
/// centred column of at most 720 pt, with padding that shrinks with the window.
struct PageGeometry: Equatable {
    static let maxTextWidth: CGFloat = 720
    static let maxPadding: CGFloat = 72
    static let minPadding: CGFloat = 28
    static let deskMargin: CGFloat = 24
    static let topPadding: CGFloat = 60
    static let bottomPadding: CGFloat = 72
    /// Space above the first page and between pages.
    static let pageGap: CGFloat = 36
    static let blockSpacing: CGFloat = 12
    static let cornerRadius: CGFloat = 8
    /// Room around the sheet for its shadow.
    static let shadowInset: CGFloat = 16

    let pageWidth: CGFloat
    let padding: CGFloat
    let pageX: CGFloat

    init(documentWidth: CGFloat) {
        let available = max(documentWidth - 2 * Self.deskMargin, 240)
        pageWidth = min(available, Self.maxTextWidth + 2 * Self.maxPadding)
        padding = min(Self.maxPadding, max(Self.minPadding, (pageWidth - Self.maxTextWidth) / 2, pageWidth * 0.06))
        pageX = ((documentWidth - pageWidth) / 2).rounded()
    }

    var textWidth: CGFloat { pageWidth - 2 * padding }

    /// Empty and short pages still look like a sheet.
    var minPageHeight: CGFloat { (pageWidth * 0.5).rounded() }
}

/// Rough page heights for pages whose views haven't been created yet, so
/// the scroller is about right before every page is laid out.
enum HeightEstimate {
    static let bodyLineHeight: CGFloat = 24
    static let averageCharacterWidth: CGFloat = 7.4

    static func text(_ text: String, width: CGFloat) -> CGFloat {
        let perLine = max(1, Int(width / averageCharacterWidth))
        var height: CGFloat = 0
        for line in text.split(separator: "\n", omittingEmptySubsequences: false) {
            let scale: CGFloat
            switch MarkdownLine(line).kind {
            case .heading(1): scale = 1.9
            case .heading(2): scale = 1.5
            case .heading(3): scale = 1.25
            default: scale = 1
            }
            let wrapped = max(1, (Int(CGFloat(line.count) * scale) + perLine - 1) / perLine)
            height += CGFloat(wrapped) * bodyLineHeight * scale
        }
        return max(height, bodyLineHeight)
    }
}
