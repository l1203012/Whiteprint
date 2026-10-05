import Foundation
import XCTest
@testable import WhiteprintRender

final class MarkdownSyntaxTests: XCTestCase {
    private func kind(_ line: String) -> MarkdownLineKind {
        MarkdownSyntax.classify(line, openFence: nil).line.kind
    }

    private func marker(_ line: String) -> Int {
        MarkdownSyntax.classify(line, openFence: nil).line.markerLength
    }

    func testHeadings() {
        XCTAssertEqual(kind("# Title"), .heading(level: 1))
        XCTAssertEqual(kind("### Sub"), .heading(level: 3))
        XCTAssertEqual(marker("## Two"), 3)
        XCTAssertEqual(kind("#hashtag"), .paragraph)
        XCTAssertEqual(kind("####### seven"), .paragraph)
        XCTAssertEqual(kind("    # indented code"), .paragraph)
    }

    func testLists() {
        XCTAssertEqual(kind("- item"), .bullet)
        XCTAssertEqual(kind("* item"), .bullet)
        XCTAssertEqual(kind("  + nested"), .bullet)
        XCTAssertEqual(marker("  - nested"), 4)
        XCTAssertEqual(kind("12. twelve"), .ordered)
        XCTAssertEqual(kind("3) three"), .ordered)
        XCTAssertEqual(marker("10. ten"), 4)
        XCTAssertEqual(kind("-not a list"), .paragraph)
        XCTAssertEqual(kind("2024.10 is a date"), .paragraph)
    }

    func testChecklists() {
        XCTAssertEqual(kind("- [ ] open"), .task(checked: false))
        XCTAssertEqual(kind("- [x] done"), .task(checked: true))
        XCTAssertEqual(kind("* [X] done"), .task(checked: true))
        XCTAssertEqual(marker("- [ ] open"), 6)
        XCTAssertEqual(kind("- [y] not a box"), .bullet)
        XCTAssertEqual(kind("- [ ]"), .task(checked: false))
    }

    func testQuotesAndDividers() {
        XCTAssertEqual(kind("> quoted"), .quote)
        XCTAssertEqual(marker("> > nested"), 4)
        XCTAssertEqual(kind("---"), .divider)
        XCTAssertEqual(kind("* * *"), .divider)
        XCTAssertEqual(kind("___"), .divider)
        XCTAssertEqual(kind("--"), .paragraph)
        XCTAssertEqual(kind(""), .blank)
        XCTAssertEqual(kind("   "), .blank)
    }

    func testFences() {
        let open = MarkdownSyntax.classify("```swift", openFence: nil)
        XCTAssertEqual(open.line.kind, .fence)
        let fence = try? XCTUnwrap(open.openFence)
        XCTAssertEqual(fence, MarkdownFenceState(marker: 0x60, length: 3))

        XCTAssertEqual(MarkdownSyntax.classify("# not a heading", openFence: fence).line.kind, .code)
        XCTAssertNotNil(MarkdownSyntax.classify("``", openFence: fence).openFence, "too short to close")
        XCTAssertNotNil(MarkdownSyntax.classify("~~~", openFence: fence).openFence, "other marker")
        let close = MarkdownSyntax.classify("````  ", openFence: fence)
        XCTAssertEqual(close.line.kind, .fence)
        XCTAssertNil(close.openFence)

        XCTAssertNil(MarkdownSyntax.classify("``` a`b", openFence: nil).openFence, "backtick in info string")
        XCTAssertNotNil(MarkdownSyntax.classify("~~~~", openFence: nil).openFence)
    }

    func testFenceStateRoundTripsThroughItsEncoding() {
        let state = MarkdownFenceState(marker: 0x7E, length: 4)
        XCTAssertEqual(MarkdownFenceState(encoded: state.encoded), state)
        XCTAssertNil(MarkdownFenceState(encoded: ""))
    }

    // MARK: Inline

    private func spans(_ text: String) -> [String] {
        let ns = text as NSString
        return MarkdownSyntax.spans(in: text).map { "\($0.kind):\(ns.substring(with: $0.contentRange))" }
    }

    func testEmphasis() {
        XCTAssertEqual(spans("a **bold** b"), ["bold:bold"])
        XCTAssertEqual(spans("__also__"), ["bold:also"])
        XCTAssertEqual(spans("*one* and _two_"), ["italic:one", "italic:two"])
        XCTAssertEqual(spans("***both***").sorted(), ["bold:*both", "italic:both**"])
        XCTAssertEqual(spans("snake_case_name"), [])
        XCTAssertEqual(spans("2 * 3 * 4"), [])
        XCTAssertEqual(spans("**unclosed"), [])
        XCTAssertEqual(spans(#"\*escaped\*"#), [])
    }

    func testCodeSpansHideMarkup() {
        XCTAssertEqual(spans("use `**x**` here"), ["code:**x**"])
        XCTAssertEqual(spans("``a ` b``"), ["code:a ` b"])
        XCTAssertEqual(spans("`unclosed"), [])
    }

    func testLinks() {
        let found = MarkdownSyntax.spans(in: "see [the docs](https://x.io/a_b_c) now", offset: 10)
        XCTAssertEqual(found.count, 1)
        XCTAssertEqual(found.first?.kind, .link)
        XCTAssertEqual(found.first?.url, "https://x.io/a_b_c")
        XCTAssertEqual(found.first?.contentRange, NSRange(location: 15, length: 8))
        XCTAssertEqual(found.first?.markupRanges, [NSRange(location: 14, length: 1), NSRange(location: 23, length: 21)])
        XCTAssertEqual(spans("[**bold link**](u)").sorted(), ["bold:bold link", "link:**bold link**"])
        XCTAssertEqual(spans("[not a link] (u)"), [])
    }
}
