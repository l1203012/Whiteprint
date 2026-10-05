import Foundation
import PDFKit

/// Reads a PDF one page at a time, falling back to OCR for pages without a
/// text layer (scans, slides exported as images).
enum PDFExtractor {
    /// Pages with fewer non-whitespace characters than this are OCR'd.
    static let ocrThreshold = 20

    /// PDFKit caches every page it has touched, so the document is reopened
    /// after this many pages to keep memory flat on long files.
    static let pagesPerOpen = 16

    static func extract(_ url: URL, ocr: Bool) throws -> [ExtractedUnit] {
        let pageCount = try autoreleasepool { try open(url).pageCount }
        var units: [ExtractedUnit] = []
        units.reserveCapacity(pageCount)
        for batch in stride(from: 0, to: pageCount, by: pagesPerOpen) {
            try autoreleasepool {
                let document = try open(url)
                for index in batch ..< min(batch + pagesPerOpen, pageCount) {
                    let text = autoreleasepool { document.page(at: index).map { self.text(of: $0, ocr: ocr) } ?? "" }
                    units.append(ExtractedUnit(ref: "p. \(index + 1)", text: text))
                }
            }
        }
        return units
    }

    private static func open(_ url: URL) throws -> PDFDocument {
        guard let document = PDFDocument(url: url), !document.isLocked || document.unlock(withPassword: "") else {
            throw ExtractionError.unreadable(url.lastPathComponent)
        }
        return document
    }

    private static func text(of page: PDFPage, ocr: Bool) -> String {
        let text = page.string ?? ""
        guard ocr, nonWhitespaceCount(text) < ocrThreshold, let image = render(page) else { return text }
        let recognized = TextRecognizer.recognize(image)
        return nonWhitespaceCount(recognized) > nonWhitespaceCount(text) ? recognized : text
    }

    static func nonWhitespaceCount(_ text: String) -> Int {
        text.unicodeScalars.reduce(0) { CharacterSet.whitespacesAndNewlines.contains($1) ? $0 : $0 + 1 }
    }

    /// The longest side of a rendered page, in pixels; keeps huge pages from
    /// allocating giant bitmaps.
    static let maxRenderSide: CGFloat = 4_000

    /// Renders `page` at about 2x on white, honouring its rotation.
    static func render(_ page: PDFPage) -> CGImage? {
        guard let cgPage = page.pageRef else { return nil }
        var box = cgPage.getBoxRect(.cropBox).size
        if abs(cgPage.rotationAngle) % 180 == 90 { box = CGSize(width: box.height, height: box.width) }
        guard box.width >= 1, box.height >= 1 else { return nil }
        let scale = min(2, maxRenderSide / max(box.width, box.height))
        let width = Int(box.width * scale), height = Int(box.height * scale)
        guard let context = CGContext(
            data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0,
            space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGImageAlphaInfo.none.rawValue
        ) else { return nil }
        context.setFillColor(gray: 1, alpha: 1)
        context.fill(CGRect(x: 0, y: 0, width: width, height: height))
        context.scaleBy(x: scale, y: scale)
        let target = CGRect(origin: .zero, size: box)
        context.concatenate(cgPage.getDrawingTransform(.cropBox, rect: target, rotate: 0, preserveAspectRatio: true))
        context.drawPDFPage(cgPage)
        return context.makeImage()
    }
}
