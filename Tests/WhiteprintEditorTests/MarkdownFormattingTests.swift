import AppKit
import XCTest
import WhiteprintCore
@testable import WhiteprintEditor

final class MarkdownFormattingTests: XCTestCase {
    /// Applies `command` to `text` with the selection marked by `[` and `]`
    /// (or a caret `|`), and returns the result marked the same way.
    private func run(_ command: EditorCommand, _ marked: String) -> String? {
        var text = marked
        let selection: NSRange
        if let caret = text.range(of: "|") {
            selection = NSRange(location: text.utf16.distance(from: text.startIndex, to: caret.lowerBound), length: 0)
            text.removeSubrange(caret)
        } else {
            let open = text.range(of: "[")!
            let start = text.utf16.distance(from: text.startIndex, to: open.lowerBound)
            text.removeSubrange(open)
            let close = text.range(of: "]", options: .backwards)!
            let end = text.utf16.distance(from: text.startIndex, to: close.lowerBound)
            text.removeSubrange(close)
            selection = NSRange(location: start, length: end - start)
        }
        guard let change = MarkdownFormatting.apply(command, in: text, selection: selection) else { return nil }
        let result = change.applied(to: text) as NSString
        let s = change.selection
        if s.length == 0 {
            return result.replacingCharacters(in: s, with: "|")
        }
        return result.replacingCharacters(in: s, with: "[" + result.substring(with: s) + "]")
    }

    func testBoldWrapsAndUnwraps() {
        XCTAssertEqual(run(.bold, "a [word] b"), "a **[word]** b")
        XCTAssertEqual(run(.bold, "a **[word]** b"), "a [word] b")
        XCTAssertEqual(run(.bold, "a [**word**] b"), "a [word] b")
        XCTAssertEqual(run(.bold, "a [word ]b"), "a **[word]** b", "trailing space stays outside")
    }

    func testEmptySelectionInsertsAndRemovesAPair() {
        XCTAssertEqual(run(.bold, "a |"), "a **|**")
        XCTAssertEqual(run(.bold, "a **|**"), "a |")
        XCTAssertEqual(run(.inlineCode, "x|"), "x`|`")
    }

    func testItalicIsNotConfusedWithBold() {
        XCTAssertEqual(run(.italic, "[x]"), "_[x]_")
        XCTAssertEqual(run(.italic, "_[x]_"), "[x]")
        XCTAssertEqual(run(.italic, "__[x]__"), "___[x]___")
    }

    func testWrapEachLineOfAMultilineSelection() {
        XCTAssertEqual(run(.inlineCode, "[a\n\nb]"), "[`a`\n\n`b`]")
        XCTAssertEqual(run(.inlineCode, "[`a`\n`b`]"), "[a\nb]")
    }

    func testNoInlineFormattingInsideCode() {
        XCTAssertNil(run(.bold, "```\n[x]\n```"))
    }

    func testHeadingsReplaceAndToggle() {
        XCTAssertEqual(run(.heading1, "Ti|tle"), "# Ti|tle")
        XCTAssertEqual(run(.heading2, "# Ti|tle"), "## Ti|tle")
        XCTAssertEqual(run(.heading2, "## Ti|tle"), "Ti|tle")
        XCTAssertEqual(run(.heading1, "- [ ] ta|sk"), "# ta|sk")
        XCTAssertEqual(run(.body, "### Ti|tle"), "Ti|tle")
        XCTAssertNil(run(.body, "Ti|tle"))
    }

    func testListsApplyToEverySelectedLine() {
        XCTAssertEqual(run(.bulletList, "[a\nb]"), "[- a\n- b]")
        XCTAssertEqual(run(.bulletList, "[- a\n- b]"), "[a\nb]")
        XCTAssertEqual(run(.numberedList, "[a\n\nb\nc]"), "[1. a\n\n2. b\n3. c]")
        XCTAssertEqual(run(.checklist, "    - it|em"), "    - [ ] it|em", "list indent is kept")
        XCTAssertEqual(run(.quote, "|said"), "> |said")
    }

    func testCodeBlockFencesTheLines() {
        XCTAssertEqual(run(.codeBlock, "|"), "```\n|\n```")
        XCTAssertEqual(run(.codeBlock, "x\n[let a\nlet b]"), "x\n```\n[let a\nlet b]\n```")
    }

    func testDividerAfterTheLine() {
        XCTAssertEqual(run(.divider, "text|"), "text\n\n---\n|")
        XCTAssertEqual(run(.divider, "|"), "---\n|")
    }

    func testInsertCommandsAreNotTextEdits() {
        XCTAssertNil(run(.drawing, "|"))
        XCTAssertNil(run(.flashcards, "|"))
        XCTAssertNil(run(.newPage, "|"))
    }

    // MARK: Decks

    func testDeckDraft() {
        var draft = DeckDraft(CardDeck(id: "c1", title: "T", cards: [Flashcard(question: "Q1", answer: "A1")]))
        XCTAssertEqual(draft.addCard(), 1)
        draft.cards[1].question = "  Q2 "
        draft.cards[1].ref = " "
        draft.addCard()
        XCTAssertTrue(draft.moveCard(at: 1, by: -1))
        XCTAssertFalse(draft.moveCard(at: 0, by: -1))
        draft.title = "  "
        XCTAssertEqual(draft.deck, CardDeck(title: nil, cards: [Flashcard(question: "Q2", answer: ""),
                                                                Flashcard(question: "Q1", answer: "A1")]))
        draft.removeCard(at: 0)
        XCTAssertEqual(draft.deck.cards.map(\.question), ["Q1"])
    }

    // MARK: Geometry

    func testA4FitsTheWidthWithPrintMargins() {
        let wide = PageGeometry(documentWidth: 2000, mode: .a4, paper: PageGeometry.a4Paper)
        XCTAssertEqual(wide.fontSize, PageGeometry.paperFontSize * PageGeometry.maxPaperScale, accuracy: 0.1)
        XCTAssertEqual(wide.pageWidth, 595.28 * 1.3, accuracy: 4)
        XCTAssertEqual(wide.padding, 56 * 1.3, accuracy: 1)
        let narrow = PageGeometry(documentWidth: 500, mode: .a4, paper: PageGeometry.a4Paper)
        XCTAssertLessThanOrEqual(narrow.pageWidth, 500 - 2 * PageGeometry.deskMargin)
        XCTAssertEqual(narrow.textWidth / narrow.fontSize, (595.28 - 112) / 11, accuracy: 1,
                       "text wraps as on the printed page")
    }

    func testPaperByRegion() {
        XCTAssertEqual(PageGeometry.paperSize(region: "US"), PageGeometry.letterPaper)
        XCTAssertEqual(PageGeometry.paperSize(region: "CA"), PageGeometry.letterPaper)
        XCTAssertEqual(PageGeometry.paperSize(region: "BE"), PageGeometry.a4Paper)
        XCTAssertEqual(PageGeometry.paperSize(region: nil), PageGeometry.a4Paper)
    }

    func testA4SheetsGrowByPrintedPages() throws {
        let geometry = PageGeometry(documentWidth: 1000, mode: .a4, paper: PageGeometry.a4Paper)
        let printable = try XCTUnwrap(geometry.printableHeight)
        XCTAssertEqual(geometry.sheet(contentHeight: 10).height, geometry.minPageHeight)
        XCTAssertEqual(geometry.sheet(contentHeight: 10).breaks, [])
        let two = geometry.sheet(contentHeight: printable + 1)
        XCTAssertEqual(two.breaks, [geometry.topPadding + printable])
        XCTAssertEqual(two.height, 2 * geometry.topPadding + 2 * printable, accuracy: 1.5)
        XCTAssertEqual(geometry.sheet(contentHeight: 3 * printable).breaks.count, 2)
    }

    func testSlidesAreUnchanged() {
        let geometry = PageGeometry(documentWidth: 1000)
        XCTAssertEqual(geometry.textWidth, PageGeometry.maxTextWidth)
        XCTAssertEqual(geometry.fontSize, 15)
        XCTAssertEqual(geometry.sheet(contentHeight: 1000).height, 60 + 1000 + 72)
        XCTAssertEqual(geometry.sheet(contentHeight: 1000).breaks, [])
    }

    // MARK: Concealed glyphs

    func testConcealedGlyphMapping() {
        XCTAssertEqual(ConcealedGlyphs.glyph(for: .hidden, glyph: 5, property: [], bullet: 9).1, .controlCharacter)
        XCTAssertEqual(ConcealedGlyphs.glyph(for: .rule, glyph: 5, property: [], bullet: 9).1, .controlCharacter)
        XCTAssertEqual(ConcealedGlyphs.glyph(for: .bullet, glyph: 5, property: [], bullet: 9).0, 9)
        XCTAssertEqual(ConcealedGlyphs.glyph(for: .checkbox, glyph: 5, property: [], bullet: 9).0, 5)
        XCTAssertEqual(ConcealedGlyphs.glyph(for: nil, glyph: 5, property: .elastic, bullet: 9).1, .elastic)
        XCTAssertNotNil(ConcealedGlyphs.bulletGlyph(in: .systemFont(ofSize: 15)))
    }

    func testRevealedRangeFollowsEdits() {
        let text = "aa\nbb\ncc" as NSString
        let revealed = NSRange(location: 3, length: 3)
        XCTAssertEqual(BlockTextView.range(revealed, afterEditing: NSRange(location: 0, length: 2), delta: 2, in: "xxaa\nbb\ncc"),
                       NSRange(location: 5, length: 3), "edit before moves it")
        XCTAssertEqual(BlockTextView.range(revealed, afterEditing: NSRange(location: 7, length: 1), delta: 1, in: text),
                       revealed, "edit after leaves it")
        XCTAssertEqual(BlockTextView.range(revealed, afterEditing: NSRange(location: 4, length: 1), delta: 1, in: "aa\nbxb\ncc"),
                       NSRange(location: 3, length: 4), "edit inside grows it")
    }
}
