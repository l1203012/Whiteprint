import AppKit
import WhiteprintRender

/// TextKit 1 layout for a text block that draws concealed Markdown markup
/// (`MarkdownStyler.markupKey`): hidden markup becomes zero-width control
/// glyphs, bullets are drawn as `•`, and checkboxes and divider rules are
/// drawn over their place.
/// The text itself stays raw Markdown, so editing, copying and hit-testing
/// work on the real characters.
final class MarkdownLayoutManager: NSLayoutManager, NSLayoutManagerDelegate {
    var palette = BlueprintPalette.blueprint

    override init() {
        super.init()
        delegate = self
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    // MARK: Glyphs

    func layoutManager(
        _ layoutManager: NSLayoutManager, shouldGenerateGlyphs glyphs: UnsafePointer<CGGlyph>,
        properties: UnsafePointer<NSLayoutManager.GlyphProperty>, characterIndexes: UnsafePointer<Int>,
        font: NSFont, forGlyphRange glyphRange: NSRange
    ) -> Int {
        let count = glyphRange.length
        guard let storage = textStorage, count > 0 else { return 0 }
        let first = characterIndexes[0], last = characterIndexes[count - 1]
        guard Self.containsMarkup(storage, in: NSRange(location: first, length: last - first + 1)) else { return 0 }
        var newGlyphs = Array(UnsafeBufferPointer(start: glyphs, count: count))
        var newProperties = Array(UnsafeBufferPointer(start: properties, count: count))
        let bullet = ConcealedGlyphs.bulletGlyph(in: font)
        for i in 0..<count {
            let value = storage.attribute(MarkdownStyler.markupKey, at: characterIndexes[i], effectiveRange: nil) as? String
            (newGlyphs[i], newProperties[i]) = ConcealedGlyphs.glyph(
                for: value.flatMap(MarkdownStyler.Markup.init), glyph: newGlyphs[i], property: newProperties[i], bullet: bullet)
        }
        setGlyphs(newGlyphs, properties: newProperties, characterIndexes: characterIndexes, font: font, forGlyphRange: glyphRange)
        return count
    }

    /// Hidden markup takes no space. (Null glyphs would, at the start of a
    /// line, be laid out on the line before, making the line look wrapped.)
    func layoutManager(
        _ layoutManager: NSLayoutManager, shouldUse action: NSLayoutManager.ControlCharacterAction,
        forControlCharacterAt charIndex: Int
    ) -> NSLayoutManager.ControlCharacterAction {
        let value = textStorage?.attribute(MarkdownStyler.markupKey, at: charIndex, effectiveRange: nil) as? String
        return ConcealedGlyphs.isZeroWidth(value.flatMap(MarkdownStyler.Markup.init)) ? .zeroAdvancement : action
    }

    private static func containsMarkup(_ storage: NSTextStorage, in range: NSRange) -> Bool {
        var found = false
        storage.enumerateAttribute(MarkdownStyler.markupKey, in: range, options: .longestEffectiveRangeNotRequired) { value, _, stop in
            if value != nil {
                found = true
                stop.pointee = true
            }
        }
        return found
    }

    // MARK: Drawing

    override func drawGlyphs(forGlyphRange glyphsToShow: NSRange, at origin: NSPoint) {
        super.drawGlyphs(forGlyphRange: glyphsToShow, at: origin)
        guard let storage = textStorage else { return }
        let characters = characterRange(forGlyphRange: glyphsToShow, actualGlyphRange: nil)
        storage.enumerateAttribute(MarkdownStyler.markupKey, in: characters) { value, range, _ in
            switch (value as? String).flatMap(MarkdownStyler.Markup.init) {
            case .checkbox: drawCheckbox(at: range, checked: false, origin: origin)
            case .checkedBox: drawCheckbox(at: range, checked: true, origin: origin)
            case .rule: drawRule(at: range, origin: origin)
            case .hidden, .bullet, nil: break
            }
        }
    }

    private func drawCheckbox(at range: NSRange, checked: Bool, origin: NSPoint) {
        guard let storage = textStorage, let container = textContainers.first,
              let font = storage.attribute(.font, at: range.location, effectiveRange: nil) as? NSFont else { return }
        let glyphs = glyphRange(forCharacterRange: range, actualCharacterRange: nil)
        let bounds = boundingRect(forGlyphRange: glyphs, in: container)
        let baseline = lineFragmentRect(forGlyphAt: glyphs.location, effectiveRange: nil).minY
            + location(forGlyphAt: glyphs.location).y
        let side = (font.capHeight * 1.3).rounded()
        let box = NSRect(x: (origin.x + bounds.minX + 1).rounded() + 0.5,
                         y: (origin.y + baseline - font.capHeight / 2 - side / 2).rounded() + 0.5,
                         width: side - 1, height: side - 1)
        let path = NSBezierPath(roundedRect: box, xRadius: 3, yRadius: 3)
        if checked {
            palette.checkbox.setFill()
            path.fill()
            let tick = NSBezierPath()
            tick.move(to: NSPoint(x: box.minX + side * 0.24, y: box.midY + side * 0.02))
            tick.line(to: NSPoint(x: box.minX + side * 0.42, y: box.maxY - side * 0.26))
            tick.line(to: NSPoint(x: box.maxX - side * 0.22, y: box.minY + side * 0.26))
            tick.lineWidth = 1.7
            tick.lineCapStyle = .round
            tick.lineJoinStyle = .round
            palette.pageBackground.setStroke()
            tick.stroke()
        } else {
            palette.text.withAlphaComponent(0.75).setStroke()
            path.lineWidth = 1.2
            path.stroke()
        }
    }

    private func drawRule(at range: NSRange, origin: NSPoint) {
        guard let container = textContainers.first else { return }
        let glyphs = glyphRange(forCharacterRange: range, actualCharacterRange: nil)
        guard glyphs.length > 0 else { return }
        let line = lineFragmentUsedRect(forGlyphAt: glyphs.location, effectiveRange: nil)
        let y = (origin.y + line.midY).rounded() + 0.5
        let rule = NSBezierPath()
        rule.move(to: NSPoint(x: origin.x, y: y))
        rule.line(to: NSPoint(x: origin.x + container.size.width - container.lineFragmentPadding * 2, y: y))
        rule.lineWidth = 1
        palette.text.withAlphaComponent(0.35).setStroke()
        rule.stroke()
    }
}

/// Which glyph a concealed character gets.
enum ConcealedGlyphs {
    /// Hidden markup and rules become control glyphs, which aren't drawn
    /// and (see `isZeroWidth`) take no space; a bullet gets the font's `•`;
    /// everything else is left alone.
    static func glyph(
        for markup: MarkdownStyler.Markup?, glyph: CGGlyph, property: NSLayoutManager.GlyphProperty, bullet: CGGlyph?
    ) -> (CGGlyph, NSLayoutManager.GlyphProperty) {
        switch markup {
        case .hidden, .rule: return (glyph, .controlCharacter)
        case .bullet: return (bullet ?? glyph, property)
        case .checkbox, .checkedBox, nil: return (glyph, property)
        }
    }

    static func isZeroWidth(_ markup: MarkdownStyler.Markup?) -> Bool {
        markup == .hidden || markup == .rule
    }

    static func bulletGlyph(in font: NSFont) -> CGGlyph? {
        var character: UniChar = 0x2022
        var glyph: CGGlyph = 0
        return CTFontGetGlyphsForCharacters(font as CTFont, &character, &glyph, 1) ? glyph : nil
    }
}
