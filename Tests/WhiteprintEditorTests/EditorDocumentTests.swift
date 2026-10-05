import Foundation
import XCTest
import WhiteprintCore
@testable import WhiteprintEditor

final class EditorDocumentTests: XCTestCase {
    private func document(_ text: String) throws -> EditorDocument {
        EditorDocument(note: try Note(parsing: text))
    }

    private func contents(_ document: EditorDocument) -> [[String]] {
        document.pages.map { page in
            page.blocks.map { block in
                switch block.content {
                case .text(let text): return "t:\(text)"
                case .drawing(let drawing): return "d:\(drawing.id):\(drawing.source)"
                }
            }
        }
    }

    func testEveryPageEndsWithATextBlock() throws {
        let doc = try document("A\n\n```wp id=d1\nbox a\n```\n+++page\n+++page\n```wp id=d2\nbox b\n```")
        XCTAssertEqual(contents(doc), [
            ["t:A", "d:d1:box a", "t:"],
            ["t:"],
            ["d:d2:box b", "t:"],
        ])
    }

    func testNoteDropsEmptyTextAndRoundTrips() throws {
        let note = try Note(parsing: "# Title\n\n```wp id=d1\nbox a\n```\n+++page\nTwo")
        let doc = EditorDocument(note: note)
        XCTAssertEqual(doc.note, note)
        XCTAssertEqual(doc.note.serialized(), note.serialized())
    }

    func testNoteTrimsSurroundingBlankLines() throws {
        var doc = try document("A")
        let id = doc.pages[0].blocks[0].id
        doc.setText("\nA\nB\n\n", of: id)
        XCTAssertEqual(doc.note.pages[0].blocks, [.text("A\nB")])
    }

    func testInsertDrawingSplitsTextAtOffset() throws {
        var doc = try document("first line\nsecond line")
        let text = doc.pages[0].blocks[0].id
        let drawing = try XCTUnwrap(doc.insertDrawing(source: "box a", splitting: text, at: 11))
        XCTAssertEqual(contents(doc), [["t:first line", "d:d1:box a", "t:second line"]])
        XCTAssertEqual(doc.pages[0].blocks[0].id, text)
        XCTAssertEqual(doc.pages[0].blocks[1].id, drawing)
        XCTAssertEqual(doc.note.serialized(), "---\nwhiteprint: 1\nlast-drawing: 1\n---\n\nfirst line\n\n```wp id=d1\nbox a\n```\n\nsecond line\n")
    }

    func testInsertDrawingAtStartPutsItBeforeTheText() throws {
        var doc = try document("text")
        let text = doc.pages[0].blocks[0].id
        doc.insertDrawing(source: "", splitting: text, at: 0)
        XCTAssertEqual(contents(doc), [["d:d1:", "t:text"]])
        XCTAssertEqual(doc.pages[0].blocks[1].id, text)
    }

    func testDrawingIDsAreNeverReused() throws {
        var doc = try document("```wp id=d1\nbox a\n```")
        let first = doc.pages[0].blocks[0].id
        doc.removeBlock(first)
        doc.appendDrawing(source: "box b", toPage: 0)
        XCTAssertEqual(doc.note.drawings.map(\.id), ["d2"])
    }

    func testAppendDrawingGoesBeforeTrailingEmptyText() throws {
        var doc = try document("A\n+++page\nB")
        doc.appendDrawing(source: "box a", toPage: 1)
        XCTAssertEqual(contents(doc), [["t:A"], ["t:B", "d:d1:box a", "t:"]])
    }

    func testInsertAndRemovePages() throws {
        var doc = try document("A\n+++page\nB")
        let new = doc.insertPage(after: 0)
        XCTAssertEqual(doc.location(of: new), BlockLocation(page: 1, block: 0))
        XCTAssertEqual(contents(doc), [["t:A"], ["t:"], ["t:B"]])
        doc.removePage(1)
        XCTAssertEqual(contents(doc), [["t:A"], ["t:B"]])
        doc.removePage(0)
        doc.removePage(0)
        XCTAssertEqual(contents(doc), [["t:B"]])
    }

    func testMergePageWithPrevious() throws {
        var doc = try document("A\n\n```wp id=d1\nx\n```\n+++page\nB")
        let b = doc.pages[1].blocks[0].id
        doc.mergePageWithPrevious(1)
        XCTAssertEqual(contents(doc), [["t:A", "d:d1:x", "t:B"]])
        XCTAssertEqual(doc.pages[0].blocks[2].id, b)
    }

    func testRemoveBlockReturnsNeighbourAndCollapsesText() throws {
        var doc = try document("A\n\n```wp id=d1\nx\n```")
        let a = doc.pages[0].blocks[0].id
        let drawing = doc.pages[0].blocks[1].id
        XCTAssertEqual(doc.removeBlock(drawing), a)
        XCTAssertEqual(contents(doc), [["t:A"]])
    }

    func testDuplicateDrawingGetsFreshID() throws {
        var doc = try document("```wp id=d4\nbox a\n```")
        let copy = try XCTUnwrap(doc.duplicateBlock(doc.pages[0].blocks[0].id))
        XCTAssertEqual(doc.block(copy)?.drawing, Drawing(id: "d5", source: "box a"))
        XCTAssertEqual(contents(doc), [["d:d4:box a", "d:d5:box a", "t:"]])
    }

    func testMoveBlockWithinAndAcrossPages() throws {
        var doc = try document("A\n\n```wp id=d1\nx\n```\n\nB\n+++page\nC")
        let drawing = doc.pages[0].blocks[1].id
        XCTAssertTrue(doc.moveBlock(drawing, by: -1))
        XCTAssertEqual(contents(doc), [["d:d1:x", "t:A", "t:B"], ["t:C"]])
        XCTAssertTrue(doc.moveBlock(drawing, by: -1) == false)
        let b = doc.pages[0].blocks[2].id
        XCTAssertTrue(doc.moveBlock(b, by: 1))
        XCTAssertEqual(contents(doc), [["d:d1:x", "t:A"], ["t:B", "t:C"]])
        XCTAssertTrue(doc.moveBlock(b, by: -1))
        XCTAssertEqual(contents(doc), [["d:d1:x", "t:A", "t:B"], ["t:C"]])
    }

    func testTrailingEmptyTextIsNotASwapPartner() throws {
        var doc = try document("```wp id=d1\nx\n```")
        let drawing = doc.pages[0].blocks[0].id
        XCTAssertFalse(doc.moveBlock(drawing, by: 1))
        XCTAssertEqual(contents(doc), [["d:d1:x", "t:"]])
    }

    func testMergeWithPreviousNeedsTextBefore() throws {
        var doc = try document("```wp id=d1\nx\n```\n\nA")
        XCTAssertNil(doc.mergeWithPrevious(doc.pages[0].blocks[1].id))
        XCTAssertNil(doc.mergeWithPrevious(doc.pages[0].blocks[0].id))
    }

    func testMergeTwoTextBlocks() throws {
        var doc = try document("A\n\n```wp id=d1\nx\n```\n\nB")
        let a = doc.pages[0].blocks[0].id
        let b = doc.pages[0].blocks[2].id
        doc.removeBlock(doc.pages[0].blocks[1].id)
        XCTAssertEqual(contents(doc), [["t:A", "t:B"]])
        let merged = try XCTUnwrap(doc.mergeWithPrevious(b))
        XCTAssertEqual(merged.block, a)
        XCTAssertEqual(merged.offset, 2)
        XCTAssertEqual(contents(doc), [["t:A\nB"]])
    }

    func testTextBlockAfterDrawing() throws {
        var doc = try document("```wp id=d1\nx\n```\n\n```wp id=d2\ny\n```")
        let first = doc.pages[0].blocks[0].id
        let inserted = try XCTUnwrap(doc.textBlock(after: first))
        XCTAssertEqual(doc.location(of: inserted), BlockLocation(page: 0, block: 1))
        let last = doc.pages[0].blocks[2].id
        XCTAssertEqual(doc.textBlock(after: last), doc.pages[0].blocks[3].id)
    }

    func testNeighboursCrossPages() throws {
        let doc = try document("A\n+++page\n```wp id=d1\nx\n```")
        let a = doc.pages[0].blocks[0].id
        XCTAssertEqual(doc.block(after: a)?.drawing?.id, "d1")
        XCTAssertEqual(doc.block(before: doc.pages[1].blocks[0].id)?.id, a)
        XCTAssertNil(doc.block(before: a))
    }

    // MARK: Reconciliation

    func testReconcileKeepsIDsOfMatchingBlocks() throws {
        let doc = try document("A\n\n```wp id=d1\nx\n```\n\nB\n+++page\nC")
        let ids = doc.pages.map { $0.blocks.map(\.id) }
        let pageIDs = doc.pages.map(\.id)
        let edited = try Note(parsing: "A2\n\n```wp id=d0\nnew\n```\n\n```wp id=d1\nx2\n```\n\nB\n+++page\nC\n+++page\nD")
        let result = doc.reconciled(with: edited)
        XCTAssertEqual(result.pages.count, 3)
        XCTAssertEqual(result.pages[0].blocks[0].id, ids[0][0], "first text block by position")
        XCTAssertNotEqual(result.pages[0].blocks[1].id, ids[0][1], "new drawing is new")
        XCTAssertEqual(result.pages[0].blocks[2].id, ids[0][1], "drawing matched by id")
        XCTAssertEqual(result.pages[0].blocks[3].id, ids[0][2], "second text block by position")
        XCTAssertEqual(result.pages[1].blocks[0].id, ids[1][0])
        XCTAssertEqual(Array(result.pages.map(\.id).prefix(2)), pageIDs)
        XCTAssertEqual(result.pages[0].blocks[0].text, "A2")
        XCTAssertEqual(result.note.pages[0].blocks[2], .drawing(Drawing(id: "d1", source: "x2")))
    }

    func testReconcileKeepsEditorTextWhenOnlyBlankLinesDiffer() throws {
        var doc = try document("A")
        let id = doc.pages[0].blocks[0].id
        doc.setText("A\n", of: id)
        let result = doc.reconciled(with: doc.note)
        XCTAssertEqual(result.pages[0].blocks[0].text, "A\n")
        XCTAssertEqual(result.pages[0].blocks[0].id, id)
    }

    func testReconcileNewIDsDontCollide() throws {
        let doc = try document("A")
        let result = doc.reconciled(with: try Note(parsing: "X\n+++page\nY\n+++page\nZ"))
        let all = result.pages.flatMap { $0.blocks.map(\.id) }
        XCTAssertEqual(Set(all).count, all.count)
    }

    func testRestoringKeepsDrawingHighWaterMark() throws {
        var doc = try document("A")
        let before = doc
        doc.appendDrawing(source: "x", toPage: 0)
        var restored = doc.restoring(before)
        XCTAssertEqual(restored.note.drawings, [])
        restored.appendDrawing(source: "y", toPage: 0)
        XCTAssertEqual(restored.note.drawings.map(\.id), ["d2"])
    }
}
