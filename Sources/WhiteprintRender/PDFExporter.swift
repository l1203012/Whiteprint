import AppKit
import WhiteprintCore

public enum PDFExporter {
    static let margin: CGFloat = 56
    static let fontSize: CGFloat = 11
    static let a4 = CGSize(width: 595.28, height: 841.89)
    static let letter = CGSize(width: 612, height: 792)

    /// A4 or US Letter by locale, typeset text only (drawings are left out),
    /// one or more PDF pages per note page, page breaks at `+++page`.
    /// Note pages without any text are skipped, except the first.
    /// Blueprint pages carry a title block dated `date`.
    public static func data(for note: Note, style: PDFExportStyle, date: Date = Date()) -> Data {
        let palette = style == .blueprint ? BlueprintPalette.blueprint : .print
        let pageSize = paperSize(region: Locale.current.region?.identifier)
        let title = note.frontMatter.title.flatMap { $0.isEmpty ? nil : $0 }
        let data = NSMutableData()
        var mediaBox = CGRect(origin: .zero, size: pageSize)
        var info: [CFString: Any] = [kCGPDFContextCreator: "Whiteprint"]
        info[kCGPDFContextTitle] = title
        guard let consumer = CGDataConsumer(data: data as CFMutableData),
              let context = CGContext(consumer: consumer, mediaBox: &mediaBox, info as CFDictionary)
        else { return Data() }

        var typesetter = PDFTypesetter(pageSize: pageSize)
        for (index, text) in pageTexts(of: note).enumerated() {
            let content = NSMutableAttributedString()
            if index == 0, let title, !text.hasPrefix("# \(title)") {
                content.append(titleString(title, palette: palette, followedByText: !text.isEmpty))
            }
            content.append(MarkdownStyler.presentation(markdown: text, palette: palette, fontSize: fontSize))
            typesetter.typeset(content)
        }
        let painter = PDFPagePainter(context: context, pageSize: pageSize, palette: palette, style: style)
        for (index, page) in typesetter.pages.enumerated() {
            autoreleasepool {
                let block = BlueprintBackground.TitleBlock(
                    title: title ?? "Untitled", page: index + 1, pageCount: typesetter.pages.count, date: date
                )
                painter.draw(page, titleBlock: block)
            }
        }
        context.closePDF()
        return data as Data
    }

    /// US Letter in the US and Canada, A4 elsewhere.
    static func paperSize(region: String?) -> CGSize {
        region == "US" || region == "CA" ? letter : a4
    }

    /// The text of each note page with drawings left out. Text-less pages
    /// after the first are dropped.
    static func pageTexts(of note: Note) -> [String] {
        let texts = note.pages.map { page in
            page.blocks.compactMap { block -> String? in
                if case .text(let text) = block { return text }
                return nil
            }.joined(separator: "\n\n")
        }
        return texts.enumerated().filter { $0.offset == 0 || !$0.element.allSatisfy(\.isWhitespace) }.map(\.element)
    }

    private static func titleString(_ title: String, palette: BlueprintPalette, followedByText: Bool) -> NSAttributedString {
        let paragraph = NSMutableParagraphStyle()
        paragraph.paragraphSpacing = fontSize * 1.6
        return NSAttributedString(string: title + (followedByText ? "\n" : ""), attributes: [
            .font: NSFont.systemFont(ofSize: 26, weight: .bold),
            .foregroundColor: palette.text,
            .paragraphStyle: paragraph,
        ])
    }
}

/// Flows attributed text over as many PDF pages as it needs. Pages are laid
/// out before any is drawn, so each page knows the page count.
private struct PDFTypesetter {
    /// The text of one PDF page.
    struct Page {
        let storage: NSTextStorage
        let layout: NSLayoutManager
        let glyphs: NSRange
    }

    let pageSize: CGSize
    private(set) var pages: [Page] = []

    init(pageSize: CGSize) {
        self.pageSize = pageSize
    }

    /// Starts a new page and adds pages until all of `text` is placed.
    mutating func typeset(_ text: NSAttributedString) {
        let storage = NSTextStorage(attributedString: text)
        let layout = NSLayoutManager()
        storage.addLayoutManager(layout)
        let margin = PDFExporter.margin
        let textSize = CGSize(width: pageSize.width - 2 * margin, height: pageSize.height - 2 * margin)
        var placed = 0
        repeat {
            let container = NSTextContainer(size: textSize)
            container.lineFragmentPadding = 0
            layout.addTextContainer(container)
            let range = layout.glyphRange(for: container)
            pages.append(Page(storage: storage, layout: layout, glyphs: range))
            guard range.length > 0 else { break }
            placed = NSMaxRange(range)
        } while placed < layout.numberOfGlyphs
    }
}

/// Draws laid-out pages on blueprint or white paper.
private struct PDFPagePainter {
    let context: CGContext
    let pageSize: CGSize
    let palette: BlueprintPalette
    let style: PDFExportStyle

    func draw(_ page: PDFTypesetter.Page, titleBlock: BlueprintBackground.TitleBlock) {
        context.beginPDFPage(nil)
        context.saveGState()
        context.translateBy(x: 0, y: pageSize.height)
        context.scaleBy(x: 1, y: -1)
        let paper = CGRect(origin: .zero, size: pageSize)
        switch style {
        case .blueprint:
            BlueprintBackground.draw(in: context, rect: paper, palette: palette, options: .init(frame: true))
        case .print:
            BlueprintBackground.draw(in: context, rect: paper, palette: palette, options: .plain)
        }
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: context, flipped: true)
        let origin = CGPoint(x: PDFExporter.margin, y: PDFExporter.margin)
        page.layout.drawBackground(forGlyphRange: page.glyphs, at: origin)
        page.layout.drawGlyphs(forGlyphRange: page.glyphs, at: origin)
        drawRules(page, origin: origin)
        switch style {
        case .blueprint: BlueprintBackground.drawTitleBlock(titleBlock, in: context, rect: paper, palette: palette)
        case .print: drawPageNumber(titleBlock.page)
        }
        NSGraphicsContext.restoreGraphicsState()
        context.restoreGState()
        context.endPDFPage()
    }

    /// Horizontal rules for the divider lines of presentation text.
    private func drawRules(_ page: PDFTypesetter.Page, origin: CGPoint) {
        let layout = page.layout
        let characters = layout.characterRange(forGlyphRange: page.glyphs, actualGlyphRange: nil)
        let width = pageSize.width - 2 * PDFExporter.margin
        page.storage.enumerateAttribute(MarkdownStyler.ruleKey, in: characters) { value, range, _ in
            guard value != nil else { return }
            let glyph = layout.glyphIndexForCharacter(at: range.location)
            let line = layout.lineFragmentUsedRect(forGlyphAt: glyph, effectiveRange: nil)
            let y = (origin.y + line.midY).rounded() + 0.25
            context.setStrokeColor(palette.muted.withAlphaComponent(0.5).cgColor)
            context.setLineWidth(0.5)
            context.move(to: CGPoint(x: origin.x, y: y))
            context.addLine(to: CGPoint(x: origin.x + width, y: y))
            context.strokePath()
        }
    }

    private func drawPageNumber(_ number: Int) {
        let label = NSAttributedString(string: String(number), attributes: [
            .font: NSFont.monospacedDigitSystemFont(ofSize: 9, weight: .regular),
            .foregroundColor: palette.muted,
        ])
        let size = label.size()
        label.draw(at: CGPoint(x: (pageSize.width - size.width) / 2, y: pageSize.height - PDFExporter.margin / 2 - size.height / 2))
    }
}
