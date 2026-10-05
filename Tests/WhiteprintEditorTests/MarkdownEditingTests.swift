import Foundation
import XCTest
@testable import WhiteprintEditor

final class MarkdownEditingTests: XCTestCase {
    /// Applies `change` and renders the caret as `|` (selections as `[...]`).
    private func render(_ text: String, _ change: TextChange?) -> String? {
        guard let change else { return nil }
        let result = change.applied(to: text) as NSString
        let selection = change.selection
        if selection.length == 0 {
            return result.replacingCharacters(in: selection, with: "|")
        }
        return result.replacingCharacters(in: selection, with: "[" + result.substring(with: selection) + "]")
    }

    /// `text` with `|` marking the caret.
    private func caret(_ marked: String) -> (String, NSRange) {
        let location = (marked as NSString).range(of: "|").location
        return (marked.replacingOccurrences(of: "|", with: ""), NSRange(location: location, length: 0))
    }

    private func newline(_ marked: String) -> String? {
        let (text, selection) = caret(marked)
        return render(text, MarkdownEditing.newline(in: text, selection: selection))
    }

    private func indent(_ marked: String, outdent: Bool = false) -> String? {
        let (text, selection) = caret(marked)
        return render(text, MarkdownEditing.indent(in: text, selection: selection, outdent: outdent))
    }

    // MARK: Line parsing

    func testParsesLinePrefixes() {
        XCTAssertEqual(MarkdownLine("## Title").kind, .heading(2))
        XCTAssertEqual(MarkdownLine("## Title").prefixLength, 3)
        XCTAssertEqual(MarkdownLine("#hashtag").kind, .paragraph)
        XCTAssertEqual(MarkdownLine("  - item").kind, .bullet("-"))
        XCTAssertEqual(MarkdownLine("  - item").indent, "  ")
        XCTAssertEqual(MarkdownLine("  - item").prefixLength, 4)
        XCTAssertEqual(MarkdownLine("12) x").kind, .ordered(12, delimiter: ")"))
        XCTAssertEqual(MarkdownLine("- [ ] task").kind, .checklist(checked: false, bullet: "-"))
        XCTAssertEqual(MarkdownLine("* [X] done").kind, .checklist(checked: true, bullet: "*"))
        XCTAssertEqual(MarkdownLine("- [ ]").prefixLength, 5)
        XCTAssertEqual(MarkdownLine("- [ ]").checkboxRange, NSRange(location: 2, length: 3))
        XCTAssertEqual(MarkdownLine("> quote").kind, .quote)
        XCTAssertEqual(MarkdownLine("-not a list").kind, .paragraph)
        XCTAssertEqual(MarkdownLine("2024. year").kind, .ordered(2024, delimiter: "."))
    }

    func testTextLines() {
        let lines = TextLine.all(in: "a\n\nbc\n")
        XCTAssertEqual(lines.map(\.text), ["a", "", "bc", ""])
        XCTAssertEqual(lines.map(\.start), [0, 2, 3, 6])
        XCTAssertEqual(TextLine.at(6, in: "a\n\nbc\n").range, NSRange(location: 6, length: 0))
        XCTAssertEqual(TextLine.at(4, in: "a\n\nbc\n").text, "bc")
    }

    func testCodeFenceTracking() {
        let text = "a\n```\ncode\n```\nb" as NSString
        XCTAssertFalse(CodeFence.isCode(lineAt: 0, in: text))
        XCTAssertTrue(CodeFence.isCode(lineAt: 6, in: text))
        XCTAssertTrue(CodeFence.isCode(lineAt: 11, in: text), "the closing fence line")
        XCTAssertFalse(CodeFence.isCode(lineAt: 15, in: text))
    }

    // MARK: Return

    func testReturnContinuesLists() {
        XCTAssertEqual(newline("- one|"), "- one\n- |")
        XCTAssertEqual(newline("  * one|"), "  * one\n  * |")
        XCTAssertEqual(newline("- [x] done|"), "- [x] done\n- [ ] |")
        XCTAssertEqual(newline("1. one|"), "1. one\n2. |")
        XCTAssertEqual(newline("9) nine|"), "9) nine\n10) |")
    }

    func testReturnSplitsAnItemAtTheCaret() {
        XCTAssertEqual(newline("- one| two"), "- one\n- | two")
    }

    func testReturnRenumbersFollowingItems() {
        XCTAssertEqual(newline("1. a|\n2. b\n    - nested\n3. c\n\nafter"),
                       "1. a\n2. |\n3. b\n    - nested\n4. c\n\nafter")
    }

    func testReturnOnEmptyItemEndsTheList() {
        XCTAssertEqual(newline("- one\n- |"), "- one\n|")
        XCTAssertEqual(newline("- one\n- [ ] |"), "- one\n|")
        XCTAssertEqual(newline("1. a\n2. |"), "1. a\n|")
    }

    func testReturnOnEmptyNestedItemOutdents() {
        XCTAssertEqual(newline("- a\n    - |"), "- a\n- |")
    }

    func testReturnElsewhereIsDefault() {
        XCTAssertNil(newline("plain|"))
        XCTAssertNil(newline("# Heading|"))
        XCTAssertNil(newline("|- item"), "caret inside the marker")
        XCTAssertNil(newline("```\n- in code|\n```"))
        let (text, _) = caret("- a|")
        XCTAssertNil(MarkdownEditing.newline(in: text, selection: NSRange(location: 0, length: 2)))
    }

    func testReturnClosesAnOpenedFence() {
        XCTAssertEqual(newline("```swift|"), "```swift\n|\n```")
        XCTAssertEqual(newline("text\n~~~~|"), "text\n~~~~\n|\n~~~~")
        XCTAssertNil(newline("```|\ncode\n```"), "already closed")
    }

    // MARK: Tab

    func testTabIndentsListItems() {
        XCTAssertEqual(indent("- a\n- b|"), "- a\n    - b|")
        XCTAssertEqual(indent("- a\n    - b|", outdent: true), "- a\n- b|")
        XCTAssertNil(indent("plain|"))
        XCTAssertNil(indent("- top|", outdent: true))
    }

    func testTabRenumbersOrderedItems() {
        XCTAssertEqual(indent("1. a\n2. b|"), "1. a\n    1. b|")
        XCTAssertEqual(indent("1. a\n    1. x\n    2. b|", outdent: true), "1. a\n    1. x\n2. b|")
    }

    func testTabIndentsEverySelectedListLine() {
        let text = "- a\n- b\nplain"
        let change = MarkdownEditing.indent(in: text, selection: NSRange(location: 0, length: 7), outdent: false)
        XCTAssertEqual(change?.applied(to: text), "    - a\n    - b\nplain")
        XCTAssertEqual(change?.selection, NSRange(location: 0, length: 15))
    }

    // MARK: Shortcuts and checkboxes

    func testBracketShortcutMakesAChecklist() {
        let text = "[]"
        let change = MarkdownEditing.shortcut(inserting: " ", at: NSRange(location: 2, length: 0), in: text)
        XCTAssertEqual(render(text, change), "- [ ] |")
        let indented = "  [x]"
        XCTAssertEqual(render(indented, MarkdownEditing.shortcut(inserting: " ", at: NSRange(location: 5, length: 0), in: indented)),
                       "  - [x] |")
        XCTAssertNil(MarkdownEditing.shortcut(inserting: " ", at: NSRange(location: 3, length: 0), in: "a []"))
        XCTAssertNil(MarkdownEditing.shortcut(inserting: "x", at: NSRange(location: 2, length: 0), in: "[]"))
        XCTAssertNil(MarkdownEditing.shortcut(inserting: " ", at: NSRange(location: 6, length: 0), in: "```\n[]"))
    }

    func testCheckboxToggle() {
        let text = "intro\n- [ ] task\n- [x] done"
        XCTAssertEqual(MarkdownEditing.checkboxRange(at: 10, in: text), NSRange(location: 8, length: 3))
        XCTAssertNil(MarkdownEditing.checkboxRange(at: 2, in: text))
        let keep = NSRange(location: 1, length: 0)
        XCTAssertEqual(MarkdownEditing.toggleCheckbox(at: 9, in: text, selection: keep)?.applied(to: text),
                       "intro\n- [x] task\n- [x] done")
        XCTAssertEqual(MarkdownEditing.toggleCheckbox(at: 20, in: text, selection: keep)?.applied(to: text),
                       "intro\n- [ ] task\n- [ ] done")
        XCTAssertEqual(MarkdownEditing.toggleCheckbox(at: 9, in: text, selection: keep)?.selection, keep)
    }

    // MARK: Slash commands

    private func slash(_ command: SlashCommand, _ marked: String, typed: String) -> MarkdownEditing.SlashEffect {
        let (text, selection) = caret(marked)
        let length = (typed as NSString).length
        return MarkdownEditing.apply(command, in: text, slash: NSRange(location: selection.location - length, length: length))
    }

    private func slashText(_ command: SlashCommand, _ marked: String, typed: String) -> String? {
        let (text, _) = caret(marked)
        guard case .text(let change) = slash(command, marked, typed: typed) else { return nil }
        return render(text, change)
    }

    func testSlashRewritesTheLinePrefix() {
        XCTAssertEqual(slashText(.heading1, "intro\n/h1|", typed: "/h1"), "intro\n# |")
        XCTAssertEqual(slashText(.heading2, "Title /h|", typed: "/h"), "## Title |")
        XCTAssertEqual(slashText(.heading3, "# Big /h3|", typed: "/h3"), "### Big |")
        XCTAssertEqual(slashText(.bulletList, "  thing /b|", typed: "/b"), "  - thing |")
        XCTAssertEqual(slashText(.checklist, "- item /to|", typed: "/to"), "- [ ] item |")
        XCTAssertEqual(slashText(.quote, "## said /q|", typed: "/q"), "> said |")
        XCTAssertEqual(slashText(.numberedList, "1. a\n/num|", typed: "/num"), "1. a\n2. |")
    }

    func testSlashCodeBlockAndDivider() {
        XCTAssertEqual(slashText(.codeBlock, "/code|", typed: "/code"), "```\n|\n```")
        XCTAssertEqual(slashText(.codeBlock, "let x /c|", typed: "/c"), "```\nlet x |\n```")
        XCTAssertEqual(slashText(.divider, "para\n/div|", typed: "/div"), "para\n\n---\n|")
        XCTAssertEqual(slashText(.divider, "/div|", typed: "/div"), "---\n|")
        XCTAssertEqual(slashText(.divider, "text /div|\nnext", typed: "/div"), "text \n\n---\n|\nnext")
    }

    func testSlashDrawingAndNewPageRemoveTheCommand() {
        let removal = TextChange(range: NSRange(location: 3, length: 3), replacement: "", selection: NSRange(location: 3, length: 0))
        XCTAssertEqual(slash(.drawing, "ab /dr|", typed: "/dr"), .insertDrawing(removal))
        XCTAssertEqual(slash(.newPage, "ab /ne|", typed: "/ne"), .newPage(removal))
    }

    func testSlashOpensOnlyAtLineStartOrAfterSpace() {
        XCTAssertTrue(MarkdownEditing.opensSlashMenu(at: 0, in: ""))
        XCTAssertTrue(MarkdownEditing.opensSlashMenu(at: 3, in: "ab "))
        XCTAssertTrue(MarkdownEditing.opensSlashMenu(at: 3, in: "ab\n"))
        XCTAssertFalse(MarkdownEditing.opensSlashMenu(at: 2, in: "ab"), "a/b paths")
        XCTAssertFalse(MarkdownEditing.opensSlashMenu(at: 4, in: "```\n"))
    }

    func testSlashCommandMatching() {
        XCTAssertEqual(SlashCommand.matching(""), SlashCommand.allCases)
        XCTAssertEqual(SlashCommand.matching("h2").first, .heading2)
        XCTAssertEqual(SlashCommand.matching("todo"), [.checklist])
        XCTAssertEqual(SlashCommand.matching("num").first, .numberedList)
        XCTAssertEqual(SlashCommand.matching("draw").first, .drawing)
        XCTAssertEqual(SlashCommand.matching("new p"), [.newPage])
        XCTAssertEqual(SlashCommand.matching("code").first, .codeBlock)
        XCTAssertEqual(SlashCommand.matching("zzz"), [])
    }

    func testSlashMenuStateFollowsTheText() {
        var state = SlashMenuState(slashLocation: 2)
        XCTAssertTrue(state.update(text: "a /", caret: 3))
        XCTAssertEqual(state.items.count, SlashCommand.allCases.count)
        XCTAssertTrue(state.update(text: "a /he", caret: 5))
        XCTAssertEqual(state.query, "he")
        XCTAssertEqual(state.selected, .heading1)
        state.moveSelection(by: 1)
        XCTAssertEqual(state.selected, .heading2)
        state.moveSelection(by: -2)
        XCTAssertEqual(state.selected, state.items.last)
        XCTAssertEqual(state.typedRange, NSRange(location: 2, length: 3))
        XCTAssertTrue(state.update(text: "a /zz", caret: 5), "no results stays open")
        XCTAssertNil(state.selected)
        XCTAssertFalse(state.update(text: "a /zz ", caret: 6), "closes on a space after no results")
        XCTAssertFalse(state.update(text: "a /he", caret: 2), "caret before the slash")
        XCTAssertFalse(state.update(text: "a he", caret: 4), "slash deleted")
        XCTAssertFalse(state.update(text: "a /h\nx", caret: 6), "newline")
    }

    // MARK: Caret transform

    func testCaretFollowsExternalEdits() {
        let old = "hello world"
        XCTAssertEqual(CaretTransform.transform(NSRange(location: 8, length: 0), from: old, to: "say hello world"),
                       NSRange(location: 12, length: 0))
        XCTAssertEqual(CaretTransform.transform(NSRange(location: 2, length: 0), from: old, to: "hello there world"),
                       NSRange(location: 2, length: 0))
        XCTAssertEqual(CaretTransform.transform(NSRange(location: 11, length: 0), from: old, to: "hello"),
                       NSRange(location: 5, length: 0))
        XCTAssertEqual(CaretTransform.transform(NSRange(location: 6, length: 5), from: old, to: "hello big world"),
                       NSRange(location: 6, length: 9))
    }
}
