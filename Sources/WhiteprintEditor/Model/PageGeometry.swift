import CoreGraphics
import Foundation

/// How the editor lays out note pages.
public enum PageLayoutMode: String, CaseIterable {
    /// Wide sheets that grow with their content, in a centred column.
    case slides
    /// Sheets of printer paper (A4, or US Letter in the US and Canada) with
    /// the PDF export's margins, marking where printed pages will break.
    case a4
}

/// Where pages and their text column go for a given document width.
///
/// Slides: a centred column of at most 720 pt, with padding that shrinks
/// with the window. A4: the paper scaled to fit the width (at most 1.3×),
/// with the text scaled along so lines wrap as on the printed page.
struct PageGeometry: Equatable {
    static let maxTextWidth: CGFloat = 720
    static let maxPadding: CGFloat = 72
    static let minPadding: CGFloat = 28
    static let deskMargin: CGFloat = 24
    /// Space above the first page and between pages.
    static let pageGap: CGFloat = 36
    static let cornerRadius: CGFloat = 8
    /// Room around the sheet for its shadow.
    static let shadowInset: CGFloat = 16
    static let bodyFontSize: CGFloat = 15

    /// Paper sizes and typesetting of the PDF export, in points.
    static let a4Paper = CGSize(width: 595.28, height: 841.89)
    static let letterPaper = CGSize(width: 612, height: 792)
    static let paperMargin: CGFloat = 56
    static let paperFontSize: CGFloat = 11
    static let maxPaperScale: CGFloat = 1.3
    /// The paper for this Mac's region.
    static let localPaper = paperSize(region: Locale.current.region?.identifier)

    /// US Letter in the US and Canada, A4 elsewhere (as the PDF export does).
    static func paperSize(region: String?) -> CGSize {
        region == "US" || region == "CA" ? letterPaper : a4Paper
    }

    let mode: PageLayoutMode
    let pageWidth: CGFloat
    let padding: CGFloat
    let pageX: CGFloat
    let topPadding: CGFloat
    let bottomPadding: CGFloat
    let blockSpacing: CGFloat
    let fontSize: CGFloat
    /// A4: how much of one printed sheet's height holds text, on screen.
    let printableHeight: CGFloat?
    private let paperHeight: CGFloat?

    init(documentWidth: CGFloat, mode: PageLayoutMode = .slides, paper: CGSize = PageGeometry.localPaper) {
        self.mode = mode
        let available = max(documentWidth - 2 * Self.deskMargin, 240)
        switch mode {
        case .slides:
            pageWidth = min(available, Self.maxTextWidth + 2 * Self.maxPadding)
            padding = min(Self.maxPadding, max(Self.minPadding, (pageWidth - Self.maxTextWidth) / 2, pageWidth * 0.06))
            topPadding = 60
            bottomPadding = 72
            blockSpacing = 12
            fontSize = Self.bodyFontSize
            printableHeight = nil
            paperHeight = nil
        case .a4:
            // Steps of 1/44 keep the font size on quarter points while resizing.
            let scale = max(1, (min(available / paper.width, Self.maxPaperScale) * 44).rounded(.down)) / 44
            pageWidth = (paper.width * scale).rounded()
            padding = (Self.paperMargin * scale).rounded()
            topPadding = padding
            bottomPadding = padding
            fontSize = Self.paperFontSize * scale
            blockSpacing = (12 * fontSize / Self.bodyFontSize).rounded()
            printableHeight = (paper.height - 2 * Self.paperMargin) * scale
            paperHeight = (paper.height * scale).rounded()
        }
        pageX = ((documentWidth - pageWidth) / 2).rounded()
    }

    var textWidth: CGFloat { pageWidth - 2 * padding }

    /// Empty and short pages still look like a sheet.
    var minPageHeight: CGFloat { paperHeight ?? (pageWidth * 0.5).rounded() }

    /// The height of a sheet holding `contentHeight` of blocks, and the
    /// offsets from its top where printed pages break (A4 only).
    func sheet(contentHeight: CGFloat) -> (height: CGFloat, breaks: [CGFloat]) {
        guard let printable = printableHeight, let paperHeight else {
            return (max(topPadding + contentHeight + bottomPadding, minPageHeight), [])
        }
        let sheets = max(1, Int((contentHeight / printable - 0.001).rounded(.up)))
        let breaks = (1..<sheets).map { topPadding + CGFloat($0) * printable }
        return ((paperHeight + CGFloat(sheets - 1) * printable).rounded(), breaks)
    }
}

/// Rough page heights for pages whose views haven't been created yet, so
/// the scroller is about right before every page is laid out.
enum HeightEstimate {
    static let bodyLineHeight: CGFloat = 24
    static let averageCharacterWidth: CGFloat = 7.4

    static func text(_ text: String, width: CGFloat, fontSize: CGFloat = PageGeometry.bodyFontSize) -> CGFloat {
        let size = fontSize / PageGeometry.bodyFontSize
        let perLine = max(1, Int(width / (averageCharacterWidth * size)))
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
            height += CGFloat(wrapped) * bodyLineHeight * scale * size
        }
        return max(height, bodyLineHeight * size)
    }
}
