import AppKit
import XCTest
@testable import WhiteprintRender

final class MarkdownStylerTests: XCTestCase {
    private let palette = BlueprintPalette.blueprint

    private func styled(_ markdown: String) -> NSAttributedString {
        MarkdownStyler.attributedString(markdown: markdown, palette: palette)
    }

    private func font(_ text: NSAttributedString, at index: Int) -> NSFont? {
        text.attribute(.font, at: index, effectiveRange: nil) as? NSFont
    }

    private func color(_ text: NSAttributedString, at index: Int) -> NSColor? {
        text.attribute(.foregroundColor, at: index, effectiveRange: nil) as? NSColor
    }

    private func paragraph(_ text: NSAttributedString, at index: Int) -> NSParagraphStyle? {
        text.attribute(.paragraphStyle, at: index, effectiveRange: nil) as? NSParagraphStyle
    }

    private func isBold(_ font: NSFont?) -> Bool {
        font?.fontDescriptor.symbolicTraits.contains(.bold) ?? false
    }

    private func isItalic(_ font: NSFont?) -> Bool {
        font?.fontDescriptor.symbolicTraits.contains(.italic) ?? false
    }

    private func isMono(_ font: NSFont?) -> Bool {
        font?.fontDescriptor.symbolicTraits.contains(.monoSpace) ?? false
    }

    // MARK: Blocks

    func testTextStaysRawMarkdown() {
        let markdown = "# Title\n- [x] **done**\n```\ncode\n```"
        XCTAssertEqual(styled(markdown).string, markdown)
    }

    func testBodyText() {
        let text = styled("Hello")
        XCTAssertEqual(font(text, at: 0)?.pointSize, MarkdownStyler.defaultFontSize)
        XCTAssertEqual(color(text, at: 0), palette.text)
        XCTAssertGreaterThan(paragraph(text, at: 0)?.lineSpacing ?? 0, 0)
    }

    func testHeadingSizesAndMutedMarkers() {
        let text = styled("# One\n## Two\n### Three\n#### Four")
        XCTAssertEqual(font(text, at: 2)?.pointSize, 28)
        XCTAssertEqual(font(text, at: 9)?.pointSize, 22)
        XCTAssertEqual(font(text, at: 17)?.pointSize, 18)
        XCTAssertEqual(font(text, at: 29)?.pointSize, 15)
        XCTAssertEqual(color(text, at: 0), palette.muted)
        XCTAssertEqual(color(text, at: 2), palette.text)
    }

    func testHeadingsScaleWithFontSize() {
        let text = MarkdownStyler.attributedString(markdown: "# Big", palette: palette, fontSize: 30)
        XCTAssertEqual(font(text, at: 2)?.pointSize, 56)
    }

    func testListsHaveAHangingIndent() {
        let text = styled("- item\n10. ten")
        let bullet = paragraph(text, at: 2)
        XCTAssertEqual(bullet?.firstLineHeadIndent, 0)
        XCTAssertGreaterThan(bullet?.headIndent ?? 0, 0)
        XCTAssertGreaterThan(paragraph(text, at: 10)?.headIndent ?? 0, bullet?.headIndent ?? 0)
        XCTAssertEqual(color(text, at: 0), palette.muted)
        XCTAssertEqual(color(text, at: 2), palette.text)
    }

    func testChecklists() {
        let text = styled("- [ ] open\n- [x] done")
        XCTAssertNil(text.attribute(.strikethroughStyle, at: 7, effectiveRange: nil))
        XCTAssertEqual(color(text, at: 7), palette.text)
        XCTAssertEqual(text.attribute(.strikethroughStyle, at: 17, effectiveRange: nil) as? Int, NSUnderlineStyle.single.rawValue)
        XCTAssertEqual(color(text, at: 17), palette.muted)
        XCTAssertEqual(color(text, at: 13), palette.accent, "checked box")
    }

    func testQuotesAreMutedAndIndented() {
        let text = styled("> quoted")
        XCTAssertEqual(color(text, at: 4), palette.muted)
        XCTAssertGreaterThan(paragraph(text, at: 4)?.firstLineHeadIndent ?? 0, 0)
    }

    func testDividerIsMuted() {
        XCTAssertEqual(color(styled("---"), at: 1), palette.muted)
    }

    func testCodeFencesAreMonospacedAndUnstyledInside() {
        let markdown = "```\n# **x**\n```\nafter **y**"
        let text = styled(markdown)
        let inside = (markdown as NSString).range(of: "**x**").location
        XCTAssertTrue(isMono(font(text, at: inside + 2)))
        XCTAssertFalse(isBold(font(text, at: inside + 2)))
        XCTAssertEqual(color(text, at: inside), palette.text, "markup inside code is not muted")
        XCTAssertEqual(color(text, at: 0), palette.muted, "fence line")
        XCTAssertNotNil(text.attribute(.backgroundColor, at: inside, effectiveRange: nil))
        XCTAssertEqual(text.attribute(MarkdownStyler.codeBlockKey, at: 0, effectiveRange: nil) as? Bool, true)

        let after = (markdown as NSString).range(of: "y").location
        XCTAssertTrue(isBold(font(text, at: after)))
        XCTAssertNil(text.attribute(MarkdownStyler.codeBlockKey, at: after, effectiveRange: nil))
    }

    func testUnclosedFenceRunsToTheEnd() {
        let text = styled("text\n```\n**a**\n\n# b")
        XCTAssertTrue(isMono(font(text, at: text.length - 1)))
    }

    // MARK: Inline

    func testInlineStyles() {
        let markdown = "a **b** *c* `d` [e](f)"
        let text = styled(markdown)
        let at = { (needle: String) in (markdown as NSString).range(of: needle).location }
        XCTAssertTrue(isBold(font(text, at: at("b"))))
        XCTAssertEqual(color(text, at: at("**")), palette.muted)
        XCTAssertTrue(isItalic(font(text, at: at("c"))))
        XCTAssertTrue(isMono(font(text, at: at("d"))))
        XCTAssertNotNil(text.attribute(.backgroundColor, at: at("d"), effectiveRange: nil))
        XCTAssertEqual(color(text, at: at("e")), palette.accent)
        XCTAssertEqual(text.attribute(MarkdownStyler.linkKey, at: at("e"), effectiveRange: nil) as? String, "f")
        XCTAssertEqual(color(text, at: at("(f")), palette.muted)
        XCTAssertNil(text.attribute(.link, at: at("e"), effectiveRange: nil))
    }

    func testBoldInsideHeadingKeepsHeadingSize() {
        let text = styled("# a **b**")
        XCTAssertEqual(font(text, at: 6)?.pointSize, 28)
        XCTAssertTrue(isBold(font(text, at: 6)))
    }

    func testBoldItalic() {
        let text = styled("***x***")
        XCTAssertTrue(isBold(font(text, at: 3)))
        XCTAssertTrue(isItalic(font(text, at: 3)))
    }

    // MARK: Incremental restyling

    /// Applies `edit` to a fully styled storage, restyles only the edited
    /// range and compares with styling the result from scratch.
    private func assertIncrementalMatchesFull(
        _ initial: String, replacing range: NSRange, with replacement: String,
        file: StaticString = #filePath, line: UInt = #line
    ) {
        let storage = NSTextStorage(string: initial)
        MarkdownStyler.apply(to: storage, palette: palette)
        storage.replaceCharacters(in: range, with: replacement)
        MarkdownStyler.apply(
            to: storage, in: NSRange(location: range.location, length: (replacement as NSString).length), palette: palette
        )
        let full = NSTextStorage(string: storage.string)
        MarkdownStyler.apply(to: full, palette: palette)
        XCTAssertTrue(storage.isEqual(to: full), "incremental restyle differs for \(storage.string.debugDescription)", file: file, line: line)
    }

    private let document = "# Notes\nSome **bold** text\n\n- [ ] task\n\n```\ncode **x**\n```\n\nafter *it*\n> quote"

    func testIncrementalTypingInAParagraph() {
        let at = (document as NSString).range(of: "bold").location
        assertIncrementalMatchesFull(document, replacing: NSRange(location: at, length: 0), with: "very ")
        assertIncrementalMatchesFull(document, replacing: NSRange(location: at - 2, length: 2), with: "")
    }

    func testIncrementalChangeOfBlockKind() {
        let at = (document as NSString).range(of: "Some").location
        assertIncrementalMatchesFull(document, replacing: NSRange(location: at, length: 0), with: "## ")
        assertIncrementalMatchesFull(document, replacing: NSRange(location: 0, length: 2), with: "")
        let task = (document as NSString).range(of: "[ ]").location
        assertIncrementalMatchesFull(document, replacing: NSRange(location: task + 1, length: 1), with: "x")
    }

    func testIncrementalOpeningAFenceRestylesTheLinesBelow() {
        let at = (document as NSString).range(of: "Some").location
        assertIncrementalMatchesFull(document, replacing: NSRange(location: at, length: 0), with: "```\n")
    }

    func testIncrementalRemovingAFenceRestylesTheLinesBelow() {
        assertIncrementalMatchesFull(document, replacing: (document as NSString).range(of: "```\ncode"), with: "code")
        let closing = (document as NSString).range(of: "```\n\nafter")
        assertIncrementalMatchesFull(document, replacing: NSRange(location: closing.location, length: 1), with: "")
    }

    func testIncrementalEditsInsideAndAroundCode() {
        let code = (document as NSString).range(of: "code").location
        assertIncrementalMatchesFull(document, replacing: NSRange(location: code, length: 0), with: "# ")
        assertIncrementalMatchesFull(document, replacing: NSRange(location: code, length: 0), with: "line\n")
    }

    func testIncrementalAtTheEdges() {
        let length = (document as NSString).length
        assertIncrementalMatchesFull(document, replacing: NSRange(location: length, length: 0), with: "\n")
        assertIncrementalMatchesFull(document, replacing: NSRange(location: length, length: 0), with: "\n- more")
        assertIncrementalMatchesFull(document, replacing: NSRange(location: 0, length: 0), with: "```\n")
        assertIncrementalMatchesFull(document, replacing: NSRange(location: 0, length: length), with: "")
        assertIncrementalMatchesFull(document, replacing: NSRange(location: 0, length: length), with: "x")
    }

    func testIncrementalRestyleStaysLocal() {
        let lines = (0..<200).map { "line \($0) with **bold**" }.joined(separator: "\n")
        let storage = NSTextStorage(string: lines)
        MarkdownStyler.apply(to: storage, palette: palette)
        let marker = NSAttributedString.Key("TestMarker")
        storage.addAttribute(marker, value: true, range: NSRange(location: 0, length: storage.length))
        storage.replaceCharacters(in: NSRange(location: 3, length: 0), with: "x")
        MarkdownStyler.apply(to: storage, in: NSRange(location: 3, length: 1), palette: palette)
        let last = storage.length - 1
        XCTAssertEqual(storage.attribute(marker, at: last, effectiveRange: nil) as? Bool, true)
        XCTAssertTrue(isBold(font(storage, at: last - 3)))
    }

    func testRestylingKeepsAttachments() {
        let storage = NSTextStorage(string: "a \u{FFFC} b")
        let attachment = NSTextAttachment()
        storage.addAttribute(.attachment, value: attachment, range: NSRange(location: 2, length: 1))
        MarkdownStyler.apply(to: storage, palette: palette)
        XCTAssertTrue(storage.attribute(.attachment, at: 2, effectiveRange: nil) as? NSTextAttachment === attachment)
    }

    func testOutOfRangeRequestIsClamped() {
        let storage = NSTextStorage(string: "# hi")
        MarkdownStyler.apply(to: storage, in: NSRange(location: 99, length: 5), palette: palette)
        XCTAssertEqual(font(storage, at: 3)?.pointSize, 28)
        MarkdownStyler.apply(to: NSTextStorage(), in: NSRange(location: 0, length: 0), palette: palette)
    }

    // MARK: Concealing markup

    private func concealed(_ markdown: String, revealing: NSRange? = nil) -> NSTextStorage {
        let storage = NSTextStorage(string: markdown)
        MarkdownStyler.apply(to: storage, palette: palette, concealsMarkup: true, revealing: revealing)
        return storage
    }

    /// Each run of `markupKey` as `"text=markup"`, in order.
    private func markup(_ text: NSAttributedString) -> [String] {
        var runs: [String] = []
        text.enumerateAttribute(MarkdownStyler.markupKey, in: NSRange(location: 0, length: text.length)) { value, range, _ in
            guard let value = value as? String else { return }
            runs.append((text.string as NSString).substring(with: range) + "=" + value)
        }
        return runs
    }

    func testShownMarkupIsNotMarked() {
        XCTAssertEqual(markup(styled("# a **b** `c`\n- d\n---")), [])
    }

    func testConcealedBlockMarkers() {
        let text = concealed("# Title\n- item\n- [ ] open\n- [x] done\n> quote\n1. one\n---")
        XCTAssertEqual(markup(text), [
            "# =hidden", "-=bullet", "- =hidden", "[ ]=checkbox", "- =hidden", "[x]=checkedBox", "> =hidden", "---=rule",
        ])
        let box = (text.string as NSString).range(of: "[x]").location
        XCTAssertEqual(color(text, at: box), .clear, "the box is drawn by the editor")
    }

    func testConcealedInlineMarkup() {
        let markdown = "a **b** _c_ `d` [e](f)"
        XCTAssertEqual(markup(concealed(markdown)), ["**=hidden", "**=hidden", "_=hidden", "_=hidden", "`=hidden", "`=hidden",
                                                    "[=hidden", "](f)=hidden"])
        XCTAssertTrue(isBold(font(concealed(markdown), at: 4)))
    }

    func testConcealedFenceLinesButNotCode() {
        XCTAssertEqual(markup(concealed("```swift\nlet **x**\n```")), ["```swift=hidden", "```=hidden"])
    }

    func testTheRevealedLineKeepsItsMarkup() {
        let markdown = "# One\n**two**\n# Three"
        XCTAssertEqual(markup(concealed(markdown, revealing: NSRange(location: 8, length: 0))), ["# =hidden", "# =hidden"])
        XCTAssertEqual(markup(concealed(markdown, revealing: NSRange(location: 3, length: 8))), ["# =hidden"])
        XCTAssertEqual(markup(concealed(markdown, revealing: NSRange(location: 6, length: 0))), ["# =hidden", "# =hidden"],
                       "caret at the start of a line reveals that line")
        let length = (markdown as NSString).length
        XCTAssertEqual(markup(concealed(markdown, revealing: NSRange(location: length, length: 0))), ["# =hidden", "**=hidden", "**=hidden"])
        XCTAssertEqual(markup(concealed("a\n# b\n", revealing: NSRange(location: 6, length: 0))), ["# =hidden"],
                       "caret on the empty last line")
    }

    func testConcealedListsIndentByTheShownMarker() {
        let task = paragraph(concealed("- [ ] task"), at: 7)
        let width = ("[ ] " as NSString).size(withAttributes: [.font: NSFont.systemFont(ofSize: 15)]).width
        XCTAssertEqual(task?.headIndent ?? 0, ceil(width), accuracy: 0.5)
        XCTAssertEqual(paragraph(concealed("> quote"), at: 3)?.headIndent, paragraph(concealed("> quote"), at: 3)?.firstLineHeadIndent)
    }

    func testIncrementalConcealingMatchesFull() {
        let storage = concealed(document)
        let revealing = NSRange(location: 2, length: 0)
        storage.replaceCharacters(in: NSRange(location: 2, length: 0), with: "**N**")
        MarkdownStyler.apply(to: storage, in: NSRange(location: 2, length: 5), palette: palette,
                             concealsMarkup: true, revealing: revealing)
        let full = NSTextStorage(string: storage.string)
        MarkdownStyler.apply(to: full, palette: palette, concealsMarkup: true, revealing: revealing)
        XCTAssertTrue(storage.isEqual(to: full))
    }

    // MARK: Presentation

    func testPresentationRemovesMarkup() {
        let text = MarkdownStyler.presentation(
            markdown: "# Title\nSome **bold** and [a link](u)\n- item\n- [x] done\n> quote\n```\ncode\n```\n---",
            palette: palette
        )
        XCTAssertEqual(text.string, "Title\nSome bold and a link\n• item\n☑\u{2002}done\nquote\n\ncode\n\n ")
        let ns = text.string as NSString
        XCTAssertTrue(isBold(font(text, at: ns.range(of: "bold").location)))
        XCTAssertEqual(color(text, at: ns.range(of: "a link").location), palette.accent)
        XCTAssertEqual(text.attribute(MarkdownStyler.ruleKey, at: ns.length - 1, effectiveRange: nil) as? Bool, true)
        let bulletIndent = paragraph(text, at: ns.range(of: "item").location)?.headIndent ?? 0
        let bulletWidth = ("• " as NSString).size(withAttributes: [.font: NSFont.systemFont(ofSize: 15)]).width
        XCTAssertEqual(bulletIndent, ceil(bulletWidth), accuracy: 0.5)
    }
}

final class PageThemeTests: XCTestCase {
    func testEveryThemeHasItsOwnPalette() {
        XCTAssertEqual(PageTheme.default, .paper)
        XCTAssertEqual(PageTheme.blueprint.palette.pageBackground, BlueprintPalette.blueprint.pageBackground)
        XCTAssertTrue(PageTheme.blueprint.palette.showsGrid)
        for theme in PageTheme.allCases where theme != .blueprint {
            XCTAssertFalse(theme.palette.showsGrid, "\(theme) pages are plain")
        }
        XCTAssertEqual(Set(PageTheme.allCases.map(\.title)).count, PageTheme.allCases.count)
    }
}
