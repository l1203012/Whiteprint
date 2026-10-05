import Foundation
import XCTest
@testable import WhiteprintExtract

final class TextCleanerTests: XCTestCase {
    private func clean(_ texts: [String]) -> [String] {
        TextCleaner.clean(texts.enumerated().map { ExtractedUnit(ref: "p. \($0.offset + 1)", text: $0.element) }).map(\.text)
    }

    func testCollapsesWhitespaceAndBlankLines() {
        XCTAssertEqual(clean(["  Cells \t are   small.\r\n\n\n\n Very\u{00A0}small. \n\n"]), ["Cells are small.\n\nVery small."])
    }

    func testJoinsHyphenatedLineBreaks() {
        XCTAssertEqual(clean(["The infor-\nmation is stored in DNA-\nRNA pairs.\nEnd -\nof line"]),
                       ["The information is stored in DNA-\nRNA pairs.\nEnd -\nof line"])
        XCTAssertEqual(clean(["soft\u{00AD}hyphen"]), ["softhyphen"])
    }

    func testDropsPageNumberLines() {
        XCTAssertEqual(clean(["Intro\n12\n- 13 -\nPage 4\nPage 4 of 20\n3/40\nPagina 2 van 9\nSeite 5\nIn 1990 we had 12 cases"]),
                       ["Intro\nIn 1990 we had 12 cases"])
        XCTAssertFalse(TextCleaner.isPageNumber("Chapter 3"))
        XCTAssertFalse(TextCleaner.isPageNumber("3.2 Methods"))
        XCTAssertTrue(TextCleaner.isPageNumber("p. 7"))
    }

    func testRemovesRepeatedHeadersAndFooters() {
        let pages = (1...4).map { "BIO 101 — Cell Biology\nTopic \($0) body text.\nMore about topic \($0).\nUniversity of Ghent · \($0 + 10)" }
        XCTAssertEqual(clean(pages), (1...4).map { "Topic \($0) body text.\nMore about topic \($0)." })
    }

    func testKeepsRepeatedLinesInTheMiddleOfAUnit() {
        let pages = ["Alpha\nbeta\nSee figure\ngamma\ndelta", "Epsilon\nzeta\nSee figure\neta\ntheta", "Iota\nkappa\nSee figure\nlambda\nmu"]
        XCTAssertEqual(clean(pages), pages)
    }

    func testIgnoresDigitsOnlyOnTheOutermostLines() {
        let pages = (1...3).map { "Example \($0)\nStep \($0) of the method\nDetails \($0) and more\nResult \($0)" }
        XCTAssertEqual(clean(pages), (1...3).map { "Step \($0) of the method\nDetails \($0) and more" })
    }

    func testDigitInsensitiveMatchingSkipsLongLinesAndNeverEmptiesAUnit() {
        let long = (1...4).map { "Page \($0 + 100) line: " + String(repeating: "the quick brown fox ", count: 5) + "end" }
        XCTAssertEqual(clean(long), long)
        let titles = (1...3).map { "Slide title \($0)" }
        XCTAssertEqual(clean(titles), titles)
    }

    func testKeepsLinesThatRepeatInHalfOrFewerUnits() {
        let pages = ["Draft\nOne", "Draft\nTwo", "Three", "Four"]
        XCTAssertEqual(clean(pages), pages)
    }

    func testNeedsThreeUnitsToDetectHeaders() {
        let pages = ["Course\nOne", "Course\nTwo"]
        XCTAssertEqual(clean(pages), pages)
    }

    func testDropsEmptyUnitsAndKeepsRefs() {
        let units = TextCleaner.clean([
            ExtractedUnit(ref: "p. 1", text: "One"),
            ExtractedUnit(ref: "p. 2", text: " \n 2 \n"),
            ExtractedUnit(ref: "p. 3", text: "Three"),
        ])
        XCTAssertEqual(units, [ExtractedUnit(ref: "p. 1", text: "One"), ExtractedUnit(ref: "p. 3", text: "Three")])
    }
}
