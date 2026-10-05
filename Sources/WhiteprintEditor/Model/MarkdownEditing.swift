import Foundation

/// A replacement in a text block. `range` is in the text before the change,
/// `selection` in the text after it; both are UTF-16.
struct TextChange: Equatable {
    var range: NSRange
    var replacement: String
    var selection: NSRange

    func applied(to text: String) -> String {
        (text as NSString).replacingCharacters(in: range, with: replacement)
    }
}

/// The Notion-style editing behaviours, as pure functions from text and
/// selection to a `TextChange`. Nil means "let the text view do its default".
enum MarkdownEditing {
    /// List and checklist items are indented by this much per level.
    static let indentUnit = "    "

    // MARK: Return

    /// ↩: continues a list or checklist, ends it on an empty item (outdenting
    /// a nested one first), and closes a code fence opened on this line.
    static func newline(in text: String, selection: NSRange) -> TextChange? {
        guard selection.length == 0 else { return nil }
        let ns = text as NSString
        let caret = selection.location
        let line = TextLine.at(caret, in: ns)
        guard !CodeFence.isCode(lineAt: line.start, in: ns) else { return nil }

        if let fence = CodeFence.opener(line.text), caret == line.end,
           CodeFence.isUnclosed(openingAt: line.start, in: ns) {
            let close = String(repeating: fence.marker, count: fence.length)
            return TextChange(range: NSRange(location: caret, length: 0), replacement: "\n\n" + close,
                              selection: NSRange(location: caret + 1, length: 0))
        }

        let parsed = MarkdownLine(line.text)
        guard parsed.isListItem, let marker = parsed.continuationMarker,
              caret - line.start >= parsed.prefixLength else { return nil }
        let content = (line.text as NSString).substring(from: parsed.prefixLength)

        if content.trimmingCharacters(in: .whitespaces).isEmpty {
            if !parsed.indent.isEmpty {
                return indent(in: text, selection: selection, outdent: true)
            }
            return TextChange(range: line.range, replacement: "",
                              selection: NSRange(location: line.start, length: 0))
        }

        var replacement = "\n" + parsed.indent + marker
        let newCaret = caret + (replacement as NSString).length
        var end = caret
        if case .ordered(let number, _) = parsed.kind {
            let renumbered = renumberedItems(after: line, in: ns, indent: parsed.indent, from: number + 2)
            if let last = renumbered.last {
                replacement += ns.substring(with: NSRange(location: caret, length: line.end - caret))
                replacement += renumbered.map { "\n" + $0.text }.joined()
                end = last.line.end
            }
        }
        return TextChange(range: NSRange(location: caret, length: end - caret), replacement: replacement,
                          selection: NSRange(location: newCaret, length: 0))
    }

    /// The ordered siblings after `line` at the same indent, renumbered from
    /// `first`, plus any nested lines between them (unchanged).
    private static func renumberedItems(
        after line: TextLine, in text: NSString, indent: String, from first: Int
    ) -> [(line: TextLine, text: String)] {
        var result: [(TextLine, String)] = []
        var number = first
        var location = line.end + 1
        while location <= text.length, line.end < text.length {
            let next = TextLine.at(location, in: text)
            let parsed = MarkdownLine(next.text)
            if case .ordered(_, let delimiter) = parsed.kind, parsed.indent == indent {
                let content = (next.text as NSString).substring(from: parsed.prefixLength)
                result.append((next, "\(indent)\(number)\(delimiter) \(content)"))
                number += 1
            } else if parsed.indent.count > indent.count, !next.text.allSatisfy(\.isWhitespace) {
                result.append((next, next.text))
            } else {
                break
            }
            if next.end >= text.length { break }
            location = next.end + 1
        }
        // Trailing nested lines after the last renumbered item don't need rewriting.
        while let last = result.last, last.0.text == last.1 { result.removeLast() }
        return result
    }

    // MARK: Tab

    /// Tab / ⇧Tab on list items: indents or outdents every list line the
    /// selection touches. Nil if none of them is a list item.
    static func indent(in text: String, selection: NSRange, outdent: Bool) -> TextChange? {
        let ns = text as NSString
        let end = NSMaxRange(selection)
        let lines = TextLine.all(in: ns).filter { line in
            line.end >= selection.location && (line.start < end || line.start == selection.location)
        }
        guard let first = lines.first, let last = lines.last,
              lines.contains(where: { MarkdownLine($0.text).isListItem }),
              !CodeFence.isCode(lineAt: first.start, in: ns) else { return nil }

        var newLines: [String] = []
        var delta = 0, startDelta = 0
        for line in lines {
            var parsed = MarkdownLine(line.text)
            var newText = line.text
            if parsed.isListItem {
                var indentText = parsed.indent
                if outdent {
                    indentText = removingIndentLevel(indentText)
                } else {
                    indentText += indentUnit
                }
                let content = (line.text as NSString).substring(from: (parsed.indent as NSString).length)
                newText = indentText + content
                if case .ordered(_, let delimiter) = parsed.kind {
                    let number = orderedNumber(before: line, in: ns, indent: indentText) ?? 1
                    parsed = MarkdownLine(newText)
                    let rest = (newText as NSString).substring(from: parsed.prefixLength)
                    newText = "\(indentText)\(number)\(delimiter) \(rest)"
                }
            }
            let lineDelta = (newText as NSString).length - (line.text as NSString).length
            if line == first {
                startDelta = lineDelta
            }
            delta += lineDelta
            newLines.append(newText)
        }
        guard delta != 0 || newLines != lines.map(\.text) else { return nil }
        let range = NSRange(location: first.start, length: last.end - first.start)
        let replacement = newLines.joined(separator: "\n")
        let newSelection: NSRange
        if selection.length == 0 {
            newSelection = NSRange(location: max(first.start, selection.location + startDelta), length: 0)
        } else {
            newSelection = NSRange(location: first.start, length: (replacement as NSString).length)
        }
        return TextChange(range: range, replacement: replacement, selection: newSelection)
    }

    private static func removingIndentLevel(_ indent: String) -> String {
        if indent.hasPrefix("\t") { return String(indent.dropFirst()) }
        let spaces = indent.prefix { $0 == " " }.count
        return String(indent.dropFirst(min(spaces, indentUnit.count)))
    }

    /// The number an ordered item at `indent` continues with, from the
    /// nearest earlier item of the same list at that indent.
    private static func orderedNumber(before line: TextLine, in text: NSString, indent: String) -> Int? {
        var location = line.start - 1
        while location >= 0 {
            let previous = TextLine.at(location, in: text)
            let parsed = MarkdownLine(previous.text)
            if previous.text.allSatisfy(\.isWhitespace) || parsed.indent.count < indent.count {
                return nil
            }
            if parsed.indent == indent {
                if case .ordered(let number, _) = parsed.kind { return number + 1 }
                return nil
            }
            location = previous.start - 1
        }
        return nil
    }

    // MARK: Shortcuts

    /// Typing a space after `[]`, `[ ]` or `[x]` at the start of a line turns
    /// it into a checklist item.
    static func shortcut(inserting string: String, at range: NSRange, in text: String) -> TextChange? {
        guard string == " ", range.length == 0 else { return nil }
        let ns = text as NSString
        let line = TextLine.at(range.location, in: ns)
        guard !CodeFence.isCode(lineAt: line.start, in: ns) else { return nil }
        let typed = ns.substring(with: NSRange(location: line.start, length: range.location - line.start))
        let indent = String(typed.prefix { $0 == " " || $0 == "\t" })
        let marker: String
        switch typed.dropFirst(indent.count) {
        case "[]", "[ ]": marker = "- [ ] "
        case "[x]", "[X]": marker = "- [x] "
        default: return nil
        }
        let start = line.start + (indent as NSString).length
        let changed = NSRange(location: start, length: range.location - start)
        return TextChange(range: changed, replacement: marker,
                          selection: NSRange(location: start + (marker as NSString).length, length: 0))
    }

    // MARK: Checklists

    /// The `[ ]` / `[x]` of the checklist line containing `location`.
    static func checkboxRange(at location: Int, in text: String) -> NSRange? {
        let ns = text as NSString
        let line = TextLine.at(location, in: ns)
        guard !CodeFence.isCode(lineAt: line.start, in: ns),
              let box = MarkdownLine(line.text).checkboxRange else { return nil }
        return NSRange(location: line.start + box.location, length: box.length)
    }

    /// Ticks or unticks the checklist item on the line containing `location`.
    /// The selection is left where it was.
    static func toggleCheckbox(at location: Int, in text: String, selection: NSRange) -> TextChange? {
        guard let box = checkboxRange(at: location, in: text) else { return nil }
        let checked = (text as NSString).substring(with: box) != "[ ]"
        return TextChange(range: box, replacement: checked ? "[ ]" : "[x]", selection: selection)
    }

    // MARK: Slash commands

    enum SlashEffect: Equatable {
        /// Rewrite the text.
        case text(TextChange)
        /// Remove the typed command, then insert a drawing at the caret.
        case insertDrawing(TextChange)
        /// Remove the typed command, then add a page after this one.
        case newPage(TextChange)
    }

    /// Applies `command` to the line where `/query` was typed. `slash` is the
    /// typed command including the slash.
    static func apply(_ command: SlashCommand, in text: String, slash: NSRange) -> SlashEffect {
        let ns = text as NSString
        let removal = TextChange(range: slash, replacement: "", selection: NSRange(location: slash.location, length: 0))
        let line = TextLine.at(slash.location, in: ns)
        let lineText = (line.text as NSString).replacingCharacters(
            in: NSRange(location: slash.location - line.start, length: min(slash.length, line.end - slash.location)),
            with: "")
        let caretInLine = slash.location - line.start
        let parsed = MarkdownLine(lineText)
        let content = (lineText as NSString).substring(from: parsed.prefixLength)
        let caretInContent = max(0, caretInLine - parsed.prefixLength)

        func rewrite(prefix: String) -> SlashEffect {
            let newLine = prefix + content
            let caret = line.start + (prefix as NSString).length + caretInContent
            return .text(TextChange(range: line.range, replacement: newLine, selection: NSRange(location: caret, length: 0)))
        }

        switch command {
        case .heading1: return rewrite(prefix: "# ")
        case .heading2: return rewrite(prefix: "## ")
        case .heading3: return rewrite(prefix: "### ")
        case .bulletList: return rewrite(prefix: parsed.indent + "- ")
        case .numberedList:
            let number = orderedNumber(before: line, in: ns, indent: parsed.indent) ?? 1
            return rewrite(prefix: parsed.indent + "\(number). ")
        case .checklist: return rewrite(prefix: parsed.indent + "- [ ] ")
        case .quote: return rewrite(prefix: "> ")
        case .codeBlock:
            let caret = line.start + 4 + (content.isEmpty ? 0 : caretInContent)
            return .text(TextChange(range: line.range, replacement: "```\n\(content)\n```",
                                    selection: NSRange(location: caret, length: 0)))
        case .divider:
            if content.trimmingCharacters(in: .whitespaces).isEmpty {
                let previousIsText = line.start > 0
                    && !TextLine.at(line.start - 1, in: ns).text.allSatisfy(\.isWhitespace)
                let divider = (previousIsText ? "\n" : "") + "---\n"
                return .text(TextChange(range: line.range, replacement: divider,
                                        selection: NSRange(location: line.start + (divider as NSString).length, length: 0)))
            }
            let replacement = lineText + "\n\n---\n"
            return .text(TextChange(range: line.range, replacement: replacement,
                                    selection: NSRange(location: line.start + (replacement as NSString).length, length: 0)))
        case .drawing: return .insertDrawing(removal)
        case .newPage: return .newPage(removal)
        }
    }

    /// Whether a `/` typed at `location` should open the slash menu: at the
    /// start of a line or after whitespace, outside code.
    static func opensSlashMenu(at location: Int, in text: String) -> Bool {
        let ns = text as NSString
        if location > 0 {
            let previous = ns.character(at: location - 1)
            guard previous == 0x20 || previous == 0x0A || previous == 0x09 else { return false }
        }
        return !CodeFence.isCode(lineAt: TextLine.at(location, in: ns).start, in: ns)
    }
}

/// Keeps a selection pointing at the same text when a block's content is
/// replaced from outside (Claude editing the note over MCP).
enum CaretTransform {
    static func transform(_ selection: NSRange, from old: String, to new: String) -> NSRange {
        let a = Array(old.utf16), b = Array(new.utf16)
        var prefix = 0
        while prefix < a.count, prefix < b.count, a[prefix] == b[prefix] { prefix += 1 }
        var suffix = 0
        while suffix < a.count - prefix, suffix < b.count - prefix, a[a.count - 1 - suffix] == b[b.count - 1 - suffix] {
            suffix += 1
        }
        func map(_ x: Int) -> Int {
            if x <= prefix { return min(x, b.count) }
            if x >= a.count - suffix { return x - a.count + b.count }
            return b.count - suffix
        }
        let start = map(selection.location)
        let end = map(NSMaxRange(selection))
        return NSRange(location: start, length: max(0, end - start))
    }
}
