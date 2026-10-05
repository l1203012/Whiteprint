import Foundation

/// A formatting or insert command, run with `NoteEditorView.perform(_:)`
/// from menus, the Touch Bar or shortcuts.
public enum EditorCommand: String, CaseIterable {
    case heading1, heading2, heading3, body
    case bold, italic, inlineCode
    case bulletList, numberedList, checklist, quote
    case codeBlock, divider, drawing, flashcards, newPage

    public var title: String {
        switch self {
        case .heading1: return "Heading 1"
        case .heading2: return "Heading 2"
        case .heading3: return "Heading 3"
        case .body: return "Body"
        case .bold: return "Bold"
        case .italic: return "Italic"
        case .inlineCode: return "Code"
        case .bulletList: return "Bulleted List"
        case .numberedList: return "Numbered List"
        case .checklist: return "Checklist"
        case .quote: return "Quote"
        case .codeBlock: return "Code Block"
        case .divider: return "Divider"
        case .drawing: return "Insert Drawing"
        case .flashcards: return "Insert Flashcards"
        case .newPage: return "New Page"
        }
    }

    /// An SF Symbol for buttons; headings and body use text labels instead.
    public var symbolName: String {
        switch self {
        case .heading1, .heading2, .heading3, .body: return "textformat.size"
        case .bold: return "bold"
        case .italic: return "italic"
        case .inlineCode: return "chevron.left.forwardslash.chevron.right"
        case .bulletList: return "list.bullet"
        case .numberedList: return "list.number"
        case .checklist: return "checklist"
        case .quote: return "text.quote"
        case .codeBlock: return "curlybraces"
        case .divider: return "minus"
        case .drawing: return "square.on.circle"
        case .flashcards: return "rectangle.on.rectangle.angled"
        case .newPage: return "doc.badge.plus"
        }
    }
}

/// The text-level commands as pure functions from a block's text and
/// selection to a `TextChange`. Nil means there's nothing to do.
enum MarkdownFormatting {
    /// Runs a text command. Insert commands (drawing, flashcards, new page)
    /// aren't text edits and return nil.
    static func apply(_ command: EditorCommand, in text: String, selection: NSRange) -> TextChange? {
        switch command {
        case .bold: return toggleWrap("**", in: text, selection: selection)
        case .italic: return toggleWrap("_", in: text, selection: selection)
        case .inlineCode: return toggleWrap("`", in: text, selection: selection)
        case .heading1: return setLineKind(.heading(1), in: text, selection: selection)
        case .heading2: return setLineKind(.heading(2), in: text, selection: selection)
        case .heading3: return setLineKind(.heading(3), in: text, selection: selection)
        case .body: return setLineKind(.paragraph, in: text, selection: selection)
        case .bulletList: return setLineKind(.bullet("-"), in: text, selection: selection)
        case .numberedList: return setLineKind(.ordered(1, delimiter: "."), in: text, selection: selection)
        case .checklist: return setLineKind(.checklist(checked: false, bullet: "-"), in: text, selection: selection)
        case .quote: return setLineKind(.quote, in: text, selection: selection)
        case .codeBlock: return codeBlock(in: text, selection: selection)
        case .divider:
            guard case .text(let change) = MarkdownEditing.apply(
                .divider, in: text, slash: NSRange(location: NSMaxRange(selection), length: 0)) else { return nil }
            return change
        case .drawing, .flashcards, .newPage:
            return nil
        }
    }

    // MARK: Inline

    /// Wraps the selection in `marker`, or unwraps it when it's already
    /// wrapped (markers just inside or just outside the selection). With no
    /// selection, inserts a pair with the caret between them, or removes an
    /// empty pair around the caret. A multi-line selection is wrapped line
    /// by line.
    static func toggleWrap(_ marker: String, in text: String, selection: NSRange) -> TextChange? {
        let ns = text as NSString
        let m = (marker as NSString).length
        guard !CodeFence.isCode(lineAt: TextLine.at(selection.location, in: ns).start, in: ns) else { return nil }
        if selection.length == 0 {
            let caret = selection.location
            if caret >= m, caret + m <= ns.length,
               ns.substring(with: NSRange(location: caret - m, length: 2 * m)) == marker + marker {
                return TextChange(range: NSRange(location: caret - m, length: 2 * m), replacement: "",
                                  selection: NSRange(location: caret - m, length: 0))
            }
            return TextChange(range: selection, replacement: marker + marker, selection: NSRange(location: caret + m, length: 0))
        }
        let selected = ns.substring(with: selection)
        if selected.contains("\n") {
            return wrapLines(marker, in: ns, selection: selection)
        }
        if isWrapped(selection, by: marker, in: ns) {
            let outer = NSRange(location: selection.location - m, length: selection.length + 2 * m)
            return TextChange(range: outer, replacement: selected,
                              selection: NSRange(location: outer.location, length: selection.length))
        }
        if selection.length >= 2 * m, selected.hasPrefix(marker), selected.hasSuffix(marker),
           isDelimiter(marker, in: selected as NSString, at: 0),
           isDelimiter(marker, in: selected as NSString, at: selection.length - m) {
            let inner = (selected as NSString).substring(with: NSRange(location: m, length: selection.length - 2 * m))
            return TextChange(range: selection, replacement: inner,
                              selection: NSRange(location: selection.location, length: (inner as NSString).length))
        }
        let trimmed = trimmingSpaces(selection, in: ns)
        guard trimmed.length > 0 else { return nil }
        return TextChange(range: trimmed, replacement: marker + ns.substring(with: trimmed) + marker,
                          selection: NSRange(location: trimmed.location + m, length: trimmed.length))
    }

    /// Whether `range` sits right between an opening and a closing `marker`
    /// (and not inside a longer run, e.g. `_` isn't wrapped by `**`).
    private static func isWrapped(_ range: NSRange, by marker: String, in text: NSString) -> Bool {
        let m = (marker as NSString).length
        guard range.location >= m, NSMaxRange(range) + m <= text.length else { return false }
        return isDelimiter(marker, in: text, at: range.location - m) && isDelimiter(marker, in: text, at: NSMaxRange(range))
    }

    /// `marker` at `index`, not part of a longer run of its character
    /// (a `***` run counts as both bold and italic).
    private static func isDelimiter(_ marker: String, in text: NSString, at index: Int) -> Bool {
        let m = (marker as NSString).length
        guard index >= 0, index + m <= text.length, text.substring(with: NSRange(location: index, length: m)) == marker else {
            return false
        }
        let character = text.character(at: index)
        var start = index, end = index + m
        while start > 0, text.character(at: start - 1) == character { start -= 1 }
        while end < text.length, text.character(at: end) == character { end += 1 }
        let run = end - start
        return run == m || (character == 0x2A && run == 3)
    }

    private static func wrapLines(_ marker: String, in text: NSString, selection: NSRange) -> TextChange? {
        let lines = TextLine.all(in: text).filter {
            NSIntersectionRange($0.range, selection).length > 0 || ($0.range.length == 0 && NSLocationInRange($0.start, selection))
        }
        guard let first = lines.first, let last = lines.last else { return nil }
        let parts = lines.map { line -> (TextLine, NSRange) in
            let start = max(line.start, selection.location)
            let end = min(line.end, NSMaxRange(selection))
            return (line, trimmingSpaces(NSRange(location: start, length: max(0, end - start)), in: text))
        }
        let wrapped = parts.filter { $0.1.length > 0 }
        guard !wrapped.isEmpty else { return nil }
        let m = (marker as NSString).length
        /// The part with its markers, whether they're just outside or just inside it.
        func markedRange(_ part: NSRange) -> NSRange? {
            if isWrapped(part, by: marker, in: text) {
                return NSRange(location: part.location - m, length: part.length + 2 * m)
            }
            if part.length >= 2 * m, isDelimiter(marker, in: text, at: part.location),
               isDelimiter(marker, in: text, at: NSMaxRange(part) - m) {
                return part
            }
            return nil
        }
        let unwrap = wrapped.allSatisfy { markedRange($0.1) != nil }
        var lineTexts: [String] = []
        for (line, part) in parts {
            var lineText = line.text as NSString
            if part.length > 0 {
                let local = NSRange(location: part.location - line.start, length: part.length)
                if unwrap, let marked = markedRange(part) {
                    let outer = NSRange(location: marked.location - line.start, length: marked.length)
                    let inner = NSRange(location: outer.location + m, length: outer.length - 2 * m)
                    lineText = lineText.replacingCharacters(in: outer, with: lineText.substring(with: inner)) as NSString
                } else {
                    lineText = lineText.replacingCharacters(in: local, with: marker + lineText.substring(with: local) + marker) as NSString
                }
            }
            lineTexts.append(lineText as String)
        }
        let range = NSRange(location: first.start, length: last.end - first.start)
        let replacement = lineTexts.joined(separator: "\n")
        return TextChange(range: range, replacement: replacement,
                          selection: NSRange(location: range.location, length: (replacement as NSString).length))
    }

    private static func trimmingSpaces(_ range: NSRange, in text: NSString) -> NSRange {
        var start = range.location, end = NSMaxRange(range)
        while start < end, text.character(at: start) == 0x20 { start += 1 }
        while end > start, text.character(at: end - 1) == 0x20 { end -= 1 }
        return NSRange(location: start, length: end - start)
    }

    // MARK: Lines

    /// Gives every line the selection touches the block kind `kind` (any
    /// existing heading, list or quote marker is replaced). When they all
    /// have it already, it's toggled off back to body text. Ordered lists
    /// are numbered from 1; code lines are left alone.
    static func setLineKind(_ kind: MarkdownLine.Kind, in text: String, selection: NSRange) -> TextChange? {
        let ns = text as NSString
        let end = NSMaxRange(selection)
        let lines = TextLine.all(in: ns).filter { line in
            line.end >= selection.location && (line.start < end || line.start == selection.location)
        }
        guard let first = lines.first, let last = lines.last else { return nil }
        let editable = lines.filter { line in
            !CodeFence.isCode(lineAt: line.start, in: ns)
                && !(lines.count > 1 && line.text.allSatisfy(\.isWhitespace))
        }
        guard !editable.isEmpty else { return nil }
        let toggleOff = kind != .paragraph && editable.allSatisfy { sameKind(MarkdownLine($0.text).kind, kind) }
        let target = toggleOff ? MarkdownLine.Kind.paragraph : kind

        var newTexts: [String] = []
        var mapping: [(old: TextLine, newStart: Int, oldPrefix: Int, newPrefix: Int)] = []
        var location = first.start
        var number = 1
        for line in lines {
            let parsed = MarkdownLine(line.text)
            var newText = line.text
            var oldPrefix = 0, newPrefix = 0
            if editable.contains(line) {
                let keepsIndent = isList(target) && isList(parsed.kind)
                let indent = keepsIndent ? parsed.indent : ""
                let prefix = indent + marker(for: target, number: number)
                if case .ordered = target { number += 1 }
                oldPrefix = parsed.prefixLength
                newPrefix = (prefix as NSString).length
                newText = prefix + (line.text as NSString).substring(from: parsed.prefixLength)
            }
            mapping.append((line, location, oldPrefix, newPrefix))
            newTexts.append(newText)
            location += (newText as NSString).length + 1
        }
        let replacement = newTexts.joined(separator: "\n")
        let range = NSRange(location: first.start, length: last.end - first.start)
        guard replacement != ns.substring(with: range) else { return nil }

        func map(_ position: Int) -> Int {
            guard let entry = mapping.last(where: { $0.old.start <= position }) else { return position }
            let offset = position - entry.old.start
            return entry.newStart + entry.newPrefix + max(0, offset - entry.oldPrefix)
        }
        let newSelection: NSRange
        if selection.length == 0 {
            newSelection = NSRange(location: map(selection.location), length: 0)
        } else {
            // A selection from the start of a line keeps the new marker in it.
            let start = selection.location == first.start ? first.start : map(selection.location)
            newSelection = NSRange(location: start, length: max(0, map(end) - start))
        }
        return TextChange(range: range, replacement: replacement, selection: newSelection)
    }

    private static func marker(for kind: MarkdownLine.Kind, number: Int) -> String {
        switch kind {
        case .paragraph: return ""
        case .heading(let level): return String(repeating: "#", count: level) + " "
        case .bullet(let bullet): return "\(bullet) "
        case .ordered(_, let delimiter): return "\(number)\(delimiter) "
        case .checklist(let checked, let bullet): return "\(bullet) [\(checked ? "x" : " ")] "
        case .quote: return "> "
        }
    }

    private static func isList(_ kind: MarkdownLine.Kind) -> Bool {
        switch kind {
        case .bullet, .ordered, .checklist: return true
        case .paragraph, .heading, .quote: return false
        }
    }

    /// Same kind of line, ignoring numbers, bullets and ticks.
    private static func sameKind(_ a: MarkdownLine.Kind, _ b: MarkdownLine.Kind) -> Bool {
        switch (a, b) {
        case (.paragraph, .paragraph), (.bullet, .bullet), (.ordered, .ordered), (.checklist, .checklist), (.quote, .quote):
            return true
        case let (.heading(x), .heading(y)):
            return x == y
        default:
            return false
        }
    }

    // MARK: Code blocks

    /// Fences the selected lines as a code block; on an empty line, inserts
    /// an empty block with the caret inside.
    private static func codeBlock(in text: String, selection: NSRange) -> TextChange? {
        let ns = text as NSString
        let firstLine = TextLine.at(selection.location, in: ns)
        guard !CodeFence.isCode(lineAt: firstLine.start, in: ns) else { return nil }
        let lastLine = TextLine.at(NSMaxRange(selection), in: ns)
        let range = NSRange(location: firstLine.start, length: lastLine.end - firstLine.start)
        let body = ns.substring(with: range)
        let replacement = "```\n" + body + "\n```"
        let caret = selection.length == 0 ? selection.location - firstLine.start : 0
        let newSelection = selection.length == 0
            ? NSRange(location: firstLine.start + 4 + caret, length: 0)
            : NSRange(location: firstLine.start + 4, length: (body as NSString).length)
        return TextChange(range: range, replacement: replacement, selection: newSelection)
    }
}
