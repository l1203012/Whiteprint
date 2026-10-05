import AppKit

extension MarkdownStyler {
    /// Marks a divider line in `presentation` text.
    static let ruleKey = NSAttributedString.Key("WhiteprintRule")

    /// Styled text for reading rather than editing, as in PDF export. Like
    /// `attributedString`, but with the markup taken out: bullets become `•`,
    /// checkboxes `☐`/`☑`, fence lines become blank padding lines of the code
    /// block, and dividers a blank line marked with `ruleKey` for the caller
    /// to draw a rule through.
    static func presentation(
        markdown: String, palette: BlueprintPalette, fontSize: CGFloat = defaultFontSize
    ) -> NSAttributedString {
        let text = NSMutableAttributedString(attributedString: attributedString(
            markdown: markdown, palette: palette, fontSize: fontSize
        ))
        let string = markdown as NSString
        var lines: [(range: NSRange, line: MarkdownLine)] = []
        var fence: MarkdownFenceState?
        string.enumerateSubstrings(in: NSRange(location: 0, length: string.length), options: [.byParagraphs]) { line, range, _, _ in
            let classified = MarkdownSyntax.classify(line ?? "", openFence: fence)
            fence = classified.openFence
            lines.append((range, classified.line))
        }
        let prefixFont = NSFont.systemFont(ofSize: fontSize)
        for (range, line) in lines.reversed() {
            let edits = presentationEdits(for: line, contents: string.substring(with: range))
            var length = range.length
            for edit in edits.sorted(by: { $0.range.location > $1.range.location }) {
                text.replaceCharacters(
                    in: NSRange(location: range.location + edit.range.location, length: edit.range.length),
                    with: edit.replacement
                )
                length += (edit.replacement as NSString).length - edit.range.length
            }
            let newRange = NSRange(location: range.location, length: length)
            switch line.kind {
            case .bullet, .ordered, .task, .quote:
                let prefix = edits.first { $0.isPrefix }
                let change = prefix.map { ($0.replacement as NSString).length - $0.range.length } ?? 0
                let prefixLength = line.markerLength + change
                reindent(text, line: newRange, prefixLength: prefixLength, font: prefixFont)
            case .divider:
                text.addAttribute(ruleKey, value: true, range: newRange)
            default:
                break
            }
        }
        return text
    }

    private struct Edit {
        var range: NSRange
        var replacement: String
        /// Replaces (part of) the line's leading marker.
        var isPrefix = false
    }

    private static func presentationEdits(for line: MarkdownLine, contents: String) -> [Edit] {
        let length = (contents as NSString).length
        let indent = contents.utf16.prefix { MarkdownSyntax.isWhitespace($0) }.count
        var edits: [Edit] = []
        switch line.kind {
        case .fence, .divider:
            return [Edit(range: NSRange(location: 0, length: length), replacement: line.kind == .divider ? " " : "")]
        case .code, .blank:
            return []
        case .heading, .quote:
            edits.append(Edit(range: NSRange(location: indent, length: line.markerLength - indent), replacement: "", isPrefix: true))
        case .bullet:
            edits.append(Edit(range: NSRange(location: indent, length: 1), replacement: "•", isPrefix: true))
        case .task(let checked):
            edits.append(Edit(
                range: NSRange(location: indent, length: line.markerLength - indent),
                replacement: checked ? "☑\u{2002}" : "☐\u{2002}", isPrefix: true
            ))
        case .ordered, .paragraph:
            break
        }
        let body = (contents as NSString).substring(from: line.markerLength)
        for span in MarkdownSyntax.spans(in: body, offset: line.markerLength) {
            edits += span.markupRanges.map { Edit(range: $0, replacement: "") }
        }
        return edits
    }

    /// Recomputes the hanging indent after the line's marker changed.
    private static func reindent(_ text: NSMutableAttributedString, line: NSRange, prefixLength: Int, font: NSFont) {
        guard line.length > 0,
              let current = text.attribute(.paragraphStyle, at: line.location, effectiveRange: nil) as? NSParagraphStyle,
              let style = current.mutableCopy() as? NSMutableParagraphStyle
        else { return }
        let prefix = text.attributedSubstring(from: NSRange(location: line.location, length: min(prefixLength, line.length))).string
        style.headIndent = style.firstLineHeadIndent + ceil((prefix as NSString).size(withAttributes: [.font: font]).width)
        text.addAttribute(.paragraphStyle, value: style, range: line)
    }
}
