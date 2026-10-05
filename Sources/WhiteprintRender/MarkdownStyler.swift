import AppKit

/// Markdown look shared by the editor and PDF export: headings, bold/italic,
/// inline code, code blocks, lists, checklists, quotes, links. The markup
/// characters stay in the text (editing is plain Markdown) but are muted, or,
/// when concealing, marked with `markupKey` for the editor to hide.
public enum MarkdownStyler {
    public static let defaultFontSize: CGFloat = 15

    /// Marks the characters of a fenced code block, fence lines included
    /// (value: `true`), e.g. so the editor can skip Markdown shortcuts there.
    public static let codeBlockKey = NSAttributedString.Key("WhiteprintCodeBlock")
    /// The destination of a `[link](url)`, as a `String`, on the link text.
    /// Not `.link`, so text views don't restyle or auto-open it.
    public static let linkKey = NSAttributedString.Key("WhiteprintLink")
    /// The code fence each line starts inside of, so an incremental restyle
    /// knows its context without re-reading the text above it.
    static let fenceKey = NSAttributedString.Key("WhiteprintMarkdownFence")
    /// Markup to draw differently while concealing, as a `Markup` raw value
    /// (`String`). The characters stay in the text; only their glyphs change.
    public static let markupKey = NSAttributedString.Key("WhiteprintMarkup")

    /// How a concealed piece of markup is drawn.
    public enum Markup: String {
        /// Not drawn and takes no space: `#`, `**`, backticks, `> `, fences, link targets.
        case hidden
        /// A list marker (`-`, `*`, `+`), drawn as `•`.
        case bullet
        /// The `[ ]` of an open checklist item. It keeps its width (and stays
        /// clickable) but is drawn as an empty box; its text is transparent.
        case checkbox
        /// The `[x]` of a ticked checklist item, drawn as a ticked box.
        case checkedBox
        /// A `---` divider, drawn as a horizontal rule across the line.
        case rule
    }

    /// Every attribute the styler sets; restyling removes only these, so
    /// attachments and other attributes survive.
    private static let managedKeys: [NSAttributedString.Key] = [
        .font, .foregroundColor, .backgroundColor, .paragraphStyle,
        .strikethroughStyle, .strikethroughColor, codeBlockKey, linkKey, fenceKey, markupKey,
    ]

    /// Restyles `storage` (or just the paragraphs touching `range`).
    ///
    /// An incremental restyle assumes the rest of `storage` was styled by an
    /// earlier call. It continues past `range` while a code fence opened or
    /// closed inside it changes how the following lines read.
    ///
    /// With `concealsMarkup`, markup is marked with `markupKey` instead of
    /// being shown, except on the lines `revealing` touches (the caret's or
    /// selection's paragraphs), and list indents follow the concealed markers.
    public static func apply(
        to storage: NSTextStorage, in range: NSRange? = nil,
        palette: BlueprintPalette, fontSize: CGFloat = defaultFontSize,
        concealsMarkup: Bool = false, revealing: NSRange? = nil
    ) {
        storage.beginEditing()
        let concealment = concealsMarkup ? Concealment(revealing: revealing) : nil
        restyle(storage, in: range, theme: MarkdownTheme(palette: palette, fontSize: fontSize), concealment: concealment)
        storage.endEditing()
    }

    /// Which lines hide their markup: all but those touching `revealing`.
    struct Concealment {
        var revealing: NSRange?

        /// `line` includes its line break, `contents` doesn't.
        func conceals(line: NSRange, contents: NSRange, textLength: Int) -> Bool {
            guard let revealing else { return true }
            if NSIntersectionRange(line, revealing).length > 0 || NSLocationInRange(revealing.location, line) {
                return false
            }
            // A caret at the very end of the text is on the last line, unless that ends in a line break.
            return !(revealing.location == textLength && NSMaxRange(contents) == textLength)
        }
    }

    /// A styled copy of `markdown`, for read-only display and export.
    public static func attributedString(
        markdown: String, palette: BlueprintPalette, fontSize: CGFloat = defaultFontSize
    ) -> NSAttributedString {
        let text = NSMutableAttributedString(string: markdown)
        restyle(text, in: nil, theme: MarkdownTheme(palette: palette, fontSize: fontSize), concealment: nil)
        text.removeAttribute(fenceKey, range: NSRange(location: 0, length: text.length))
        return text
    }

    // MARK: Restyling

    private static func restyle(
        _ text: NSMutableAttributedString, in range: NSRange?, theme: MarkdownTheme, concealment: Concealment?
    ) {
        let string = text.string as NSString
        let length = string.length
        guard length > 0 else { return }
        let dirty: NSRange
        if let range {
            let location = min(max(range.location, 0), length)
            dirty = string.paragraphRange(for: NSRange(location: location, length: min(range.length, length - location)))
        } else {
            dirty = NSRange(location: 0, length: length)
        }
        var fence = dirty.location > 0 ? fenceAfterLine(endingAt: dirty.location, in: text) : nil
        var location = dirty.location
        while location < length {
            let lineRange = string.paragraphRange(for: NSRange(location: location, length: 0))
            if location >= NSMaxRange(dirty), storedFence(at: location, in: text) == fence {
                break
            }
            var contentsEnd = 0
            string.getParagraphStart(nil, end: nil, contentsEnd: &contentsEnd, for: lineRange)
            let contents = NSRange(location: lineRange.location, length: contentsEnd - lineRange.location)
            let classified = MarkdownSyntax.classify(string.substring(with: contents), openFence: fence)
            let conceals = concealment?.conceals(line: lineRange, contents: contents, textLength: length) ?? false
            style(text, line: classified.line, range: lineRange, contents: contents, theme: theme, conceals: conceals)
            if let fence {
                text.addAttribute(fenceKey, value: fence.encoded, range: lineRange)
            }
            fence = classified.openFence
            location = NSMaxRange(lineRange)
        }
    }

    private static func storedFence(at location: Int, in text: NSAttributedString) -> MarkdownFenceState? {
        (text.attribute(fenceKey, at: location, effectiveRange: nil) as? String).flatMap(MarkdownFenceState.init(encoded:))
    }

    /// The fence left open after the line that ends at `location`.
    private static func fenceAfterLine(endingAt location: Int, in text: NSAttributedString) -> MarkdownFenceState? {
        let string = text.string as NSString
        let previous = string.paragraphRange(for: NSRange(location: location - 1, length: 0))
        var contentsEnd = 0
        string.getParagraphStart(nil, end: nil, contentsEnd: &contentsEnd, for: previous)
        let contents = string.substring(with: NSRange(location: previous.location, length: contentsEnd - previous.location))
        return MarkdownSyntax.classify(contents, openFence: storedFence(at: previous.location, in: text)).openFence
    }

    // MARK: Lines

    private static func style(
        _ text: NSMutableAttributedString, line: MarkdownLine, range: NSRange, contents: NSRange,
        theme: MarkdownTheme, conceals: Bool
    ) {
        for key in managedKeys {
            text.removeAttribute(key, range: range)
        }
        let marker = NSRange(location: contents.location, length: min(line.markerLength, contents.length))
        let body = NSRange(location: NSMaxRange(marker), length: contents.length - marker.length)
        let prefix = (text.string as NSString).substring(with: marker)
        let concealed = conceals ? ConcealedMarker(line: line, prefix: prefix) : nil
        let shownPrefix = concealed?.shownPrefix ?? prefix

        switch line.kind {
        case .fence, .code:
            text.addAttributes([
                .font: theme.monospaced,
                .foregroundColor: line.kind == .fence ? theme.palette.muted : theme.palette.text,
                .backgroundColor: theme.codeBlockBackground,
                .paragraphStyle: line.kind == .fence ? theme.fenceParagraph : theme.codeParagraph,
                codeBlockKey: true,
            ], range: range)
            if conceals, line.kind == .fence, contents.length > 0 {
                text.addAttribute(markupKey, value: Markup.hidden.rawValue, range: contents)
            }
            return
        case .heading(let level):
            text.addAttributes([
                .font: theme.headingFont(level: level), .foregroundColor: theme.palette.text,
                .paragraphStyle: theme.headingParagraph(level: level),
            ], range: range)
        case .bullet, .ordered, .task:
            text.addAttributes([
                .font: theme.body, .foregroundColor: theme.palette.text,
                .paragraphStyle: theme.hangingParagraph(prefix: shownPrefix, firstLineIndent: 0),
            ], range: range)
        case .quote:
            text.addAttributes([
                .font: theme.body, .foregroundColor: theme.palette.muted,
                .paragraphStyle: theme.hangingParagraph(prefix: shownPrefix, firstLineIndent: theme.quoteIndent),
            ], range: range)
        case .divider:
            text.addAttributes([
                .font: theme.body, .foregroundColor: theme.palette.muted, .paragraphStyle: theme.bodyParagraph,
            ], range: range)
            if conceals {
                text.addAttribute(markupKey, value: Markup.rule.rawValue, range: contents)
            }
            return
        case .blank, .paragraph:
            text.addAttributes([
                .font: theme.body, .foregroundColor: theme.palette.text, .paragraphStyle: theme.bodyParagraph,
            ], range: range)
        }

        styleInline(text, in: body, theme: theme, conceals: conceals)
        text.addAttribute(.foregroundColor, value: theme.palette.muted, range: marker)
        for part in concealed?.parts ?? [] {
            let range = NSRange(location: marker.location + part.range.location, length: part.range.length)
            text.addAttribute(markupKey, value: part.markup.rawValue, range: range)
            if part.markup == .checkbox || part.markup == .checkedBox {
                text.addAttribute(.foregroundColor, value: NSColor.clear, range: range)
            }
        }
        if case .task(let checked) = line.kind {
            let box = (prefix as NSString).range(of: "[")
            if checked, box.location != NSNotFound, !conceals {
                text.addAttribute(
                    .foregroundColor, value: theme.palette.accent,
                    range: NSRange(location: marker.location + box.location, length: 3)
                )
            }
            if checked, body.length > 0 {
                text.addAttributes([
                    .foregroundColor: theme.palette.muted,
                    .strikethroughStyle: NSUnderlineStyle.single.rawValue,
                    .strikethroughColor: theme.palette.muted,
                ], range: body)
            }
        }
    }

    private static func styleInline(_ text: NSMutableAttributedString, in body: NSRange, theme: MarkdownTheme, conceals: Bool) {
        guard body.length > 0 else { return }
        let spans = MarkdownSyntax.spans(in: (text.string as NSString).substring(with: body), offset: body.location)
        for span in spans {
            switch span.kind {
            case .code:
                let size = (text.attribute(.font, at: span.range.location, effectiveRange: nil) as? NSFont)?.pointSize
                text.addAttributes([
                    .font: theme.inlineCodeFont(matching: size ?? theme.fontSize),
                    .backgroundColor: theme.inlineCodeBackground,
                ], range: span.range)
            case .link:
                text.addAttribute(.foregroundColor, value: theme.palette.accent, range: span.contentRange)
                if let url = span.url {
                    text.addAttribute(linkKey, value: url, range: span.contentRange)
                }
            case .bold:
                convertFonts(in: text, range: span.contentRange) { theme.adding(.bold, to: $0) }
            case .italic:
                convertFonts(in: text, range: span.contentRange) { theme.adding(.italic, to: $0) }
            }
        }
        for span in spans {
            for markup in span.markupRanges {
                text.addAttribute(.foregroundColor, value: theme.palette.muted, range: markup)
                if conceals {
                    text.addAttribute(markupKey, value: Markup.hidden.rawValue, range: markup)
                }
            }
        }
    }

    private static func convertFonts(in text: NSMutableAttributedString, range: NSRange, _ convert: (NSFont) -> NSFont) {
        text.enumerateAttribute(.font, in: range) { value, run, _ in
            if let font = value as? NSFont {
                text.addAttribute(.font, value: convert(font), range: run)
            }
        }
    }
}

/// How a line's leading marker looks while concealed: which parts are
/// hidden or drawn differently (ranges within the marker), and the text that
/// stays visible, for the hanging indent.
struct ConcealedMarker: Equatable {
    struct Part: Equatable {
        var range: NSRange
        var markup: MarkdownStyler.Markup
    }

    var parts: [Part] = []
    var shownPrefix: String

    init(line: MarkdownLine, prefix: String) {
        let c = Array(prefix.utf16)
        let indent = c.prefix(while: MarkdownSyntax.isWhitespace).count
        let indentText = String(prefix.prefix(indent))
        func text(from start: Int) -> String {
            String(utf16CodeUnits: Array(c[start...]), count: c.count - start)
        }
        shownPrefix = prefix
        switch line.kind {
        case .heading:
            parts = c.isEmpty ? [] : [Part(range: NSRange(location: 0, length: c.count), markup: .hidden)]
            shownPrefix = ""
        case .quote:
            parts = indent < c.count ? [Part(range: NSRange(location: indent, length: c.count - indent), markup: .hidden)] : []
            shownPrefix = indentText
        case .bullet:
            guard indent < c.count else { break }
            parts = [Part(range: NSRange(location: indent, length: 1), markup: .bullet)]
            shownPrefix = indentText + "•" + text(from: indent + 1)
        case .task(let checked):
            guard let box = c.firstIndex(of: MarkdownSyntax.openBracket), box + 3 <= c.count else { break }
            parts = [
                Part(range: NSRange(location: indent, length: box - indent), markup: .hidden),
                Part(range: NSRange(location: box, length: 3), markup: checked ? .checkedBox : .checkbox),
            ]
            shownPrefix = indentText + text(from: box)
        case .blank, .paragraph, .ordered, .divider, .fence, .code:
            break
        }
    }
}

/// Fonts, colours and paragraph styles for one palette and body size.
private struct MarkdownTheme {
    let palette: BlueprintPalette
    let fontSize: CGFloat
    let body: NSFont
    let monospaced: NSFont
    let bodyParagraph: NSParagraphStyle
    /// Fence lines sit flush left so the block's background starts at the margin.
    let fenceParagraph: NSParagraphStyle
    let codeParagraph: NSParagraphStyle
    let codeBlockBackground: NSColor
    let inlineCodeBackground: NSColor
    let quoteIndent: CGFloat

    init(palette: BlueprintPalette, fontSize: CGFloat) {
        self.palette = palette
        self.fontSize = fontSize
        body = NSFont.systemFont(ofSize: fontSize)
        monospaced = NSFont.monospacedSystemFont(ofSize: (fontSize * 0.87).rounded(), weight: .regular)
        bodyParagraph = Self.paragraph(lineSpacing: fontSize * 0.35)
        fenceParagraph = Self.paragraph(lineSpacing: fontSize * 0.2)
        let code = Self.paragraph(lineSpacing: fontSize * 0.2)
        code.firstLineHeadIndent = fontSize * 0.75
        code.headIndent = fontSize * 0.75
        codeParagraph = code
        codeBlockBackground = palette.text.withAlphaComponent(0.08)
        inlineCodeBackground = palette.text.withAlphaComponent(0.13)
        quoteIndent = fontSize * 0.8
    }

    static func paragraph(lineSpacing: CGFloat) -> NSMutableParagraphStyle {
        let style = NSMutableParagraphStyle()
        style.lineSpacing = lineSpacing
        return style
    }

    /// H1/H2/H3 scale with the body size (28/22/18 pt at 15 pt); H4–H6 are bold body text.
    func headingFont(level: Int) -> NSFont {
        let scale: [CGFloat] = [28, 22, 18]
        let size = level <= 3 ? (fontSize * scale[level - 1] / 15).rounded() : fontSize
        return NSFont.systemFont(ofSize: size, weight: .semibold)
    }

    func headingParagraph(level: Int) -> NSParagraphStyle {
        let style = Self.paragraph(lineSpacing: fontSize * 0.2)
        style.paragraphSpacingBefore = fontSize * (level == 1 ? 0.6 : level == 2 ? 0.5 : 0.35)
        style.paragraphSpacing = fontSize * 0.2
        return style
    }

    /// Wrapped lines start where the text after `prefix` (indent and marker) starts.
    func hangingParagraph(prefix: String, firstLineIndent: CGFloat) -> NSParagraphStyle {
        let style = Self.paragraph(lineSpacing: fontSize * 0.35)
        style.firstLineHeadIndent = firstLineIndent
        let width = (prefix as NSString).size(withAttributes: [.font: body]).width
        style.headIndent = firstLineIndent + ceil(width)
        return style
    }

    func inlineCodeFont(matching size: CGFloat) -> NSFont {
        NSFont.monospacedSystemFont(ofSize: (size * 0.87).rounded(), weight: .regular)
    }

    func adding(_ trait: NSFontDescriptor.SymbolicTraits, to font: NSFont) -> NSFont {
        if trait == .bold, !font.fontDescriptor.symbolicTraits.contains(.monoSpace) {
            let italic = font.fontDescriptor.symbolicTraits.contains(.italic)
            let bold = NSFont.systemFont(ofSize: font.pointSize, weight: .bold)
            return italic ? adding(.italic, to: bold) : bold
        }
        let descriptor = font.fontDescriptor.withSymbolicTraits(font.fontDescriptor.symbolicTraits.union(trait))
        return NSFont(descriptor: descriptor, size: font.pointSize) ?? font
    }
}
