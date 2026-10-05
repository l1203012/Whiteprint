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
    public static func data(for note: Note, style: PDFExportStyle) -> Data {
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

        var typesetter = PDFTypesetter(context: context, pageSize: pageSize, palette: palette, style: style)
        for (index, text) in pageTexts(of: note).enumerated() {
            autoreleasepool {
                let content = NSMutableAttributedString()
                if index == 0, let title, !text.hasPrefix("# \(title)") {
                    content.append(titleString(title, palette: palette, followedByText: !text.isEmpty))
                }
                content.append(MarkdownStyler.presentation(markdown: text, palette: palette, fontSize: fontSize))
                typesetter.typeset(content)
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

/// Flows attributed text over as many PDF pages as it needs.
private struct PDFTypesetter {
    let context: CGContext
    let pageSize: CGSize
    let palette: BlueprintPalette
    let style: PDFExportStyle
    private var pageNumber = 0

    init(context: CGContext, pageSize: CGSize, palette: BlueprintPalette, style: PDFExportStyle) {
        self.context = context
        self.pageSize = pageSize
        self.palette = palette
        self.style = style
    }

    /// Starts a new PDF page and fills pages until all of `text` is placed.
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
            drawPage(layout, glyphs: range)
            guard range.length > 0 else { break }
            placed = NSMaxRange(range)
        } while placed < layout.numberOfGlyphs
    }

    private mutating func drawPage(_ layout: NSLayoutManager, glyphs: NSRange) {
        pageNumber += 1
        context.beginPDFPage(nil)
        context.saveGState()
        context.translateBy(x: 0, y: pageSize.height)
        context.scaleBy(x: 1, y: -1)
        drawPaper()
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: context, flipped: true)
        let origin = CGPoint(x: PDFExporter.margin, y: PDFExporter.margin)
        layout.drawBackground(forGlyphRange: glyphs, at: origin)
        layout.drawGlyphs(forGlyphRange: glyphs, at: origin)
        drawRules(layout, glyphs: glyphs, origin: origin)
        drawPageNumber()
        NSGraphicsContext.restoreGraphicsState()
        context.restoreGState()
        context.endPDFPage()
    }

    private func drawPaper() {
        context.setFillColor(palette.pageBackground.cgColor)
        context.fill(CGRect(origin: .zero, size: pageSize))
        guard style == .blueprint else { return }
        PageGrid.fillPattern(in: context, size: pageSize, color: palette.grid)
    }

    /// Horizontal rules for the divider lines of presentation text.
    private func drawRules(_ layout: NSLayoutManager, glyphs: NSRange, origin: CGPoint) {
        guard let storage = layout.textStorage else { return }
        let characters = layout.characterRange(forGlyphRange: glyphs, actualGlyphRange: nil)
        let width = pageSize.width - 2 * PDFExporter.margin
        storage.enumerateAttribute(MarkdownStyler.ruleKey, in: characters) { value, range, _ in
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

    private func drawPageNumber() {
        let label = NSAttributedString(string: String(pageNumber), attributes: [
            .font: NSFont.monospacedDigitSystemFont(ofSize: 9, weight: .regular),
            .foregroundColor: palette.muted,
        ])
        let size = label.size()
        label.draw(at: CGPoint(x: (pageSize.width - size.width) / 2, y: pageSize.height - PDFExporter.margin / 2 - size.height / 2))
    }
}

/// The faint square grid behind blueprint pages.
enum PageGrid {
    static func stroke(in context: CGContext, size: CGSize, spacing: CGFloat, color: NSColor, lineWidth: CGFloat = 0.5) {
        guard spacing > 0 else { return }
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
        context.setLineWidth(lineWidth)
        context.addPath(path)
        context.strokePath()
        context.restoreGState()
    }

    /// The grid in `SceneRenderer.unit` cells as one tiling pattern. In a PDF,
    /// hundreds of stroked lines make viewers split copied text into fragments.
    static func fillPattern(in context: CGContext, size: CGSize, color: NSColor) {
        let cell = SceneRenderer.unit
        var callbacks = CGPatternCallbacks(version: 0, drawPattern: { _, tile in
            tile.fill(CGRect(x: 0, y: 0, width: SceneRenderer.unit, height: 0.5))
            tile.fill(CGRect(x: 0, y: 0, width: 0.5, height: SceneRenderer.unit))
        }, releaseInfo: nil)
        guard let srgb = CGColorSpace(name: CGColorSpace.sRGB),
              let space = CGColorSpace(patternBaseSpace: srgb),
              let rgba = color.usingColorSpace(.sRGB),
              let pattern = CGPattern(
                  info: nil, bounds: CGRect(x: 0, y: 0, width: cell, height: cell),
                  matrix: .identity, xStep: cell, yStep: cell,
                  tiling: .constantSpacing, isColored: false, callbacks: &callbacks
              )
        else { return }
        var components = [rgba.redComponent, rgba.greenComponent, rgba.blueComponent, rgba.alphaComponent]
        context.saveGState()
        context.setFillColorSpace(space)
        context.setFillPattern(pattern, colorComponents: &components)
        context.fill(CGRect(origin: .zero, size: size))
        context.restoreGState()
    }
}
