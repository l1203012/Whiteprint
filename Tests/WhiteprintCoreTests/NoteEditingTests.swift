import XCTest
@testable import WhiteprintCore

final class NoteEditingTests: XCTestCase {
    private func note(_ text: String) throws -> Note {
        try Note(parsing: text)
    }

    func testPageSourceIncludesDrawingIDs() throws {
        let n = try note("Hello\n\n```wp\nbox a\n```\n+++page\nTwo")
        XCTAssertEqual(try n.pageSource(1), "Hello\n\n```wp id=d1\nbox a\n```")
        XCTAssertEqual(try n.pageSource(2), "Two")
    }

    func testPageNumbersAreChecked() throws {
        let n = try note("x")
        XCTAssertThrowsError(try n.pageSource(0)) { error in
            XCTAssertEqual((error as? NoteEditError)?.description, "page 0 doesn't exist (note has 1 page)")
        }
        XCTAssertThrowsError(try n.pageSource(2))
    }

    func testAppendAddsBlocksAndFreshDrawingIDs() throws {
        var n = try note("```wp id=d1\nbox a\n```")
        try n.write("More text\n\n```wp id=d1\nbox b\n```", page: 1, mode: .append)
        XCTAssertEqual(n.pages[0].blocks, [
            .drawing(Drawing(id: "d1", source: "box a")),
            .text("More text"),
            .drawing(Drawing(id: "d2", source: "box b")),
        ])
    }

    func testReplaceKeepsIDsOfDrawingsWrittenBack() throws {
        var n = try note("A\n\n```wp id=d1\nbox a\n```\n\n```wp id=d2\nbox b\n```\n+++page\n```wp id=d3\nbox c\n```")
        // d1 is edited and kept, d2 is dropped, d3 lives on another page so it can't be claimed.
        try n.write("B\n\n```wp id=d1\nbox a2\n```\n\n```wp id=d3\nbox x\n```\n\n```wp\nbox y\n```", page: 1, mode: .replace)
        XCTAssertEqual(n.pages[0].blocks, [
            .text("B"),
            .drawing(Drawing(id: "d1", source: "box a2")),
            .drawing(Drawing(id: "d4", source: "box x")),
            .drawing(Drawing(id: "d5", source: "box y")),
        ])
        XCTAssertEqual(n.pages[1].blocks, [.drawing(Drawing(id: "d3", source: "box c"))])
    }

    func testWritingPageSeparatorsInsertsPages() throws {
        var n = try note("one\n+++page\nlast")
        try n.write("two\n+++page\nthree", page: 1, mode: .append)
        XCTAssertEqual(n.pages.map(\.blocks), [[.text("one"), .text("two")], [.text("three")], [.text("last")]])
    }

    func testInsertUpdateDeleteDrawings() throws {
        var n = try note("```wp id=d1\nbox a\n```\n\ntext")
        let end = try n.insertDrawing("box z", page: 1)
        let after = try n.insertDrawing("box b", page: 1, after: "d1")
        XCTAssertEqual([end, after], ["d2", "d3"])
        XCTAssertEqual(n.pages[0].blocks.count, 4)
        XCTAssertEqual(n.pages[0].blocks[1], .drawing(Drawing(id: "d3", source: "box b")))

        try n.updateDrawing("d3", source: "box c")
        XCTAssertEqual(n.drawing("d3")?.drawing.source, "box c")
        XCTAssertEqual(n.drawing("d3")?.page, 1)

        try n.deleteDrawing("d3")
        XCTAssertNil(n.drawing("d3"))
        XCTAssertEqual(n.nextDrawingID(), "d4")
        XCTAssertThrowsError(try n.updateDrawing("d3", source: "")) { error in
            XCTAssertEqual(error as? NoteEditError, .unknownDrawing("d3"))
        }
        XCTAssertThrowsError(try n.insertDrawing("box", page: 1, after: "nope"))
    }

    func testAddAndRemovePages() throws {
        var n = try note("one")
        XCTAssertEqual(try n.addPage(), 2)
        XCTAssertEqual(try n.addPage(after: 1), 2)
        XCTAssertEqual(n.pages.count, 3)
        try n.removePage(2)
        try n.removePage(2)
        XCTAssertThrowsError(try n.removePage(1)) { error in
            XCTAssertEqual(error as? NoteEditError, .lastPage)
        }
    }

    func testRemovingAPageRetiresItsDrawingIDs() throws {
        var n = try note("a\n+++page\n```wp id=d1\n```")
        try n.removePage(2)
        XCTAssertEqual(n.nextDrawingID(), "d2")
    }

    // MARK: Markdown export

    func testMarkdownExport() throws {
        let n = try note("---\ntitle: Plan\n---\nIntro\n\n```wp\nbox a\n```\n+++page\nPage two")
        XCTAssertEqual(n.markdown(), "# Plan\n\nIntro\n\n*[drawing omitted]*\n\n---\n\nPage two\n")
        XCTAssertEqual(n.markdown(drawingPlaceholder: nil), "# Plan\n\nIntro\n\n---\n\nPage two\n")
    }

    func testMarkdownExportDoesNotRepeatTitleHeading() throws {
        XCTAssertEqual(try note("---\ntitle: Plan\n---\n# Plan\nx").markdown(), "# Plan\nx\n")
        XCTAssertEqual(try note("").markdown(), "")
    }
}
