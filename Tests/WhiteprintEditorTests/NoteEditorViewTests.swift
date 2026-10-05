import AppKit
import XCTest
import WhiteprintCore
@testable import WhiteprintEditor

final class NoteEditorViewTests: XCTestCase {
    private var window: NSWindow!
    private var editor: NoteEditorView!

    override func setUp() {
        _ = NSApplication.shared
    }

    override func tearDown() {
        window?.close()
        window = nil
        editor = nil
    }

    private func open(_ text: String) throws {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 900, height: 800), styleMask: [.titled, .resizable],
                          backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false
        editor = NoteEditorView(note: try Note(parsing: text))
        window.contentView = editor
        editor.layoutSubtreeIfNeeded()
    }

    private func block(_ page: Int, _ index: Int) -> EditorBlock {
        editor.document.pages[page].blocks[index]
    }

    /// Focuses a text block with the caret at `caret` (default: the end).
    @discardableResult
    private func focusText(_ page: Int, _ index: Int, caret: Int? = nil) throws -> BlockTextView {
        let block = block(page, index)
        let length = ((block.text ?? "") as NSString).length
        editor.focus(.text(block.id, NSRange(location: caret ?? length, length: 0)))
        return try XCTUnwrap(window.firstResponder as? BlockTextView)
    }

    private func type(_ text: String) throws {
        let view = try XCTUnwrap(window.firstResponder as? NSTextView)
        for character in text {
            view.insertText(String(character), replacementRange: view.selectedRange())
        }
    }

    private func press(_ key: Key, flags: NSEvent.ModifierFlags = []) throws {
        let responder = try XCTUnwrap(window.firstResponder as? NSView)
        let event = try XCTUnwrap(NSEvent.keyEvent(
            with: .keyDown, location: .zero, modifierFlags: flags.union(key.flags), timestamp: 0,
            windowNumber: window.windowNumber, context: nil, characters: key.characters,
            charactersIgnoringModifiers: key.characters, isARepeat: false, keyCode: key.code))
        responder.keyDown(with: event)
    }

    private struct Key {
        var characters: String
        var code: UInt16
        var flags: NSEvent.ModifierFlags = []

        static let enter = Key(characters: "\r", code: 36)
        static let tab = Key(characters: "\t", code: 48)
        static let backtab = Key(characters: "\u{19}", code: 48, flags: .shift)
        static let backspace = Key(characters: "\u{7f}", code: 51)
        static let escape = Key(characters: "\u{1b}", code: 53)
        static let up = Key(characters: String(UnicodeScalar(UInt16(NSUpArrowFunctionKey))!), code: 126, flags: [.function, .numericPad])
        static let down = Key(characters: String(UnicodeScalar(UInt16(NSDownArrowFunctionKey))!), code: 125, flags: [.function, .numericPad])
    }

    private var firstPageBlocks: [NoteBlock] { editor.note.pages[0].blocks }

    // MARK: Typing and Markdown behaviours

    func testTypingUpdatesTheNote() throws {
        try open("Hello")
        try focusText(0, 0)
        try type(" world")
        XCTAssertEqual(firstPageBlocks, [.text("Hello world")])
    }

    func testReturnContinuesAndEndsAChecklist() throws {
        try open("- [x] milk")
        try focusText(0, 0)
        try press(.enter)
        try type("eggs")
        XCTAssertEqual(firstPageBlocks, [.text("- [x] milk\n- [ ] eggs")])
        try press(.enter)
        try press(.enter)
        try type("done")
        XCTAssertEqual(firstPageBlocks, [.text("- [x] milk\n- [ ] eggs\ndone")])
    }

    func testTabIndentsListItems() throws {
        try open("- a\n- b")
        try focusText(0, 0)
        try press(.tab)
        XCTAssertEqual(firstPageBlocks, [.text("- a\n    - b")])
        try press(.backtab)
        XCTAssertEqual(firstPageBlocks, [.text("- a\n- b")])
    }

    func testBracketShortcutMakesAChecklist() throws {
        try open("")
        try focusText(0, 0)
        try type("[] buy")
        XCTAssertEqual(firstPageBlocks, [.text("- [ ] buy")])
    }

    func testClickingACheckboxTogglesItUndoably() throws {
        try open("- [ ] task")
        let view = try XCTUnwrap(editor.textViews[block(0, 0).id])
        editor.blockTextView(view, toggleCheckboxAt: 3)
        XCTAssertEqual(firstPageBlocks, [.text("- [x] task")])
        window.undoManager?.undo()
        XCTAssertEqual(firstPageBlocks, [.text("- [ ] task")])
    }

    func testCheckboxHitTesting() throws {
        try open("- [ ] task")
        let view = try XCTUnwrap(editor.textViews[block(0, 0).id])
        let box = view.rect(for: NSRange(location: 2, length: 3))
        XCTAssertEqual(view.checkbox(at: NSPoint(x: box.midX, y: box.midY)), NSRange(location: 2, length: 3))
        let word = view.rect(for: NSRange(location: 7, length: 2))
        XCTAssertNil(view.checkbox(at: NSPoint(x: word.midX, y: word.midY)))
    }

    // MARK: Slash menu

    func testSlashMenuTurnsALineIntoAHeading() throws {
        try open("Title")
        try focusText(0, 0)
        try type(" /h2")
        let menu = try XCTUnwrap(editor.slashMenu)
        XCTAssertEqual(menu.state.selected, .heading2)
        XCTAssertFalse(editor.slashMenuView.isHidden)
        try press(.enter)
        XCTAssertNil(editor.slashMenu)
        XCTAssertTrue(editor.slashMenuView.isHidden)
        XCTAssertEqual(firstPageBlocks, [.text("## Title ")])
    }

    func testSlashMenuArrowsAndEscape() throws {
        try open("")
        try focusText(0, 0)
        try type("/")
        XCTAssertEqual(editor.slashMenu?.state.selected, .heading1)
        try press(.down)
        try press(.down)
        XCTAssertEqual(editor.slashMenu?.state.selected, .heading3)
        try press(.up)
        XCTAssertEqual(editor.slashMenu?.state.selected, .heading2)
        try press(.escape)
        XCTAssertNil(editor.slashMenu)
        XCTAssertEqual(firstPageBlocks, [.text("/")])
    }

    func testSlashDoesNotOpenInsideAWord() throws {
        try open("and")
        try focusText(0, 0)
        try type("/or")
        XCTAssertNil(editor.slashMenu)
    }

    func testSlashDrawingSplitsTheBlockAndOpensTheEditor() throws {
        try open("First\nSecond")
        try focusText(0, 0, caret: 6)
        try type("/draw")
        try press(.enter)
        XCTAssertEqual(firstPageBlocks, [.text("First"), .drawing(Drawing(id: "d1", source: "")), .text("Second")])
        let editorID = try XCTUnwrap(editor.drawingEditor?.blockID)
        XCTAssertEqual(editor.document.block(editorID)?.drawing?.id, "d1")
        editor.drawingEditor?.commit()
    }

    func testSlashNewPageAddsAPageAfterThisOne() throws {
        try open("One\n+++page\nTwo")
        try focusText(0, 0)
        try type(" /new page")
        try press(.enter)
        XCTAssertEqual(editor.note.pages.map(\.blocks), [[.text("One ")], [], [.text("Two")]])
        let focused = try XCTUnwrap(window.firstResponder as? BlockTextView)
        XCTAssertEqual(editor.document.location(of: focused.blockID), BlockLocation(page: 1, block: 0))
    }

    // MARK: Moving between blocks

    func testArrowKeysMoveBetweenBlocksAndPages() throws {
        try open("A\n\n```wp id=d1\nbox a\n```\n\nB\n+++page\nC")
        try focusText(0, 0)
        try press(.down)
        XCTAssertTrue(window.firstResponder is DrawingBlockView)
        try press(.down)
        XCTAssertEqual((window.firstResponder as? BlockTextView)?.string, "B")
        try press(.down)
        XCTAssertEqual((window.firstResponder as? BlockTextView)?.string, "C")
        try press(.up)
        try press(.up)
        XCTAssertTrue(window.firstResponder is DrawingBlockView)
        try press(.up)
        XCTAssertEqual((window.firstResponder as? BlockTextView)?.string, "A")
    }

    func testArrowStaysInsideMultilineText() throws {
        try open("one\ntwo\nthree")
        let view = try focusText(0, 0, caret: 5)
        try press(.down)
        XCTAssertTrue(window.firstResponder === view)
        XCTAssertEqual(view.selectedRange().location, 9)
    }

    func testBackspaceSelectsThenDeletesADrawing() throws {
        try open("A\n\n```wp id=d1\nbox a\n```\n\nB")
        try focusText(0, 2, caret: 0)
        try press(.backspace)
        XCTAssertTrue(window.firstResponder is DrawingBlockView)
        try press(.backspace)
        XCTAssertEqual(firstPageBlocks, [.text("A"), .text("B")])
        XCTAssertEqual((window.firstResponder as? BlockTextView)?.string, "A")
        window.undoManager?.undo()
        XCTAssertEqual(firstPageBlocks, [.text("A"), .drawing(Drawing(id: "d1", source: "box a")), .text("B")])
        window.undoManager?.redo()
        XCTAssertEqual(firstPageBlocks, [.text("A"), .text("B")])
    }

    func testBackspaceOnAnEmptyPageRemovesIt() throws {
        try open("One\n+++page\n")
        XCTAssertEqual(editor.note.pages.count, 2)
        try focusText(1, 0)
        try press(.backspace)
        XCTAssertEqual(editor.note.pages.count, 1)
        XCTAssertEqual((window.firstResponder as? BlockTextView)?.selectedRange(), NSRange(location: 3, length: 0))
    }

    func testReturnOnASelectedDrawingStartsTextAfterIt() throws {
        try open("```wp id=d1\nx\n```\n\n```wp id=d2\ny\n```")
        editor.focus(.drawing(block(0, 0).id))
        try press(.enter)
        try type("between")
        XCTAssertEqual(firstPageBlocks, [.drawing(Drawing(id: "d1", source: "x")), .text("between"),
                                        .drawing(Drawing(id: "d2", source: "y"))])
    }

    // MARK: Block menu operations

    func testBlockMenuOperationsAreUndoable() throws {
        try open("A\n\n```wp id=d1\nx\n```")
        let drawing = block(0, 1).id
        editor.moveBlock(drawing, by: -1)
        XCTAssertEqual(firstPageBlocks, [.drawing(Drawing(id: "d1", source: "x")), .text("A")])
        window.undoManager?.undo()
        XCTAssertEqual(firstPageBlocks, [.text("A"), .drawing(Drawing(id: "d1", source: "x"))])
        editor.duplicateBlock(drawing)
        XCTAssertEqual(editor.note.drawings.map(\.id), ["d1", "d2"])
        editor.deleteBlock(block(0, 0).id)
        XCTAssertEqual(editor.note.drawings.map(\.id), ["d1", "d2"])
        XCTAssertEqual(firstPageBlocks.count, 2)
    }

    // MARK: Drawings

    func testCommittingTheDrawingEditorUpdatesTheDrawing() throws {
        try open("```wp id=d1\nbox a\n```")
        editor.editDrawing(block(0, 0).id)
        let sourceEditor = try XCTUnwrap(editor.drawingEditor)
        sourceEditor.loadView()
        editor.commitDrawing(sourceEditor.blockID, source: "box b")
        XCTAssertEqual(firstPageBlocks, [.drawing(Drawing(id: "d1", source: "box b"))])
        XCTAssertEqual(editor.drawingViews[block(0, 0).id]?.source, "box b")
        window.undoManager?.undo()
        XCTAssertEqual(firstPageBlocks, [.drawing(Drawing(id: "d1", source: "box a"))])
    }

    func testInsertDrawingAtSelection() throws {
        try open("Before after")
        try focusText(0, 0, caret: 7)
        editor.insertDrawingAtSelection(source: "box n")
        XCTAssertEqual(firstPageBlocks, [.text("Before "), .drawing(Drawing(id: "d1", source: "box n")), .text("after")])
        XCTAssertNotNil(editor.drawingEditor)
        window.makeFirstResponder(nil)
        editor.insertDrawingAtSelection(source: "box m")
        XCTAssertEqual(editor.note.drawings.map(\.id), ["d1", "d2"])
        XCTAssertEqual(firstPageBlocks.last, .drawing(Drawing(id: "d2", source: "box m")))
    }

    // MARK: API

    func testAddPageMovesTheCaretThere() throws {
        try open("One")
        try focusText(0, 0)
        editor.addPage()
        try type("Two")
        XCTAssertEqual(editor.note.pages.map(\.blocks), [[.text("One")], [.text("Two")]])
    }

    func testOnChangeIsCoalesced() throws {
        try open("")
        var received: [Note] = []
        editor.onChange = { received.append($0) }
        try focusText(0, 0)
        try type("abc")
        XCTAssertEqual(received.count, 0)
        editor.flushPendingChange()
        XCTAssertEqual(received.map { $0.pages[0].blocks }, [[.text("abc")]])
        editor.flushPendingChange()
        XCTAssertEqual(received.count, 1)
    }

    func testSetNoteKeepsTheCaretAndBlockViews() throws {
        try open("hello world\n\n```wp id=d1\nx\n```\n\ntail")
        let view = try focusText(0, 0, caret: 6)
        let drawingView = editor.drawingViews[block(0, 1).id]
        let changed = try Note(parsing: "say hello world\n\n```wp id=d1\ny\n```\n\ntail")
        editor.setNote(changed, preservingSelection: true)
        XCTAssertEqual(editor.note, changed)
        XCTAssertTrue(window.firstResponder === view)
        XCTAssertEqual(view.string, "say hello world")
        XCTAssertEqual(view.selectedRange(), NSRange(location: 10, length: 0))
        XCTAssertTrue(editor.drawingViews[block(0, 1).id] === drawingView)
        XCTAssertEqual(drawingView?.source, "y")
    }

    func testSetNoteWhenTheFocusedBlockDisappears() throws {
        try open("A\n+++page\nB")
        try focusText(1, 0, caret: 1)
        editor.setNote(try Note(parsing: "A"), preservingSelection: true)
        XCTAssertEqual((window.firstResponder as? BlockTextView)?.string, "A")
        editor.setNote(try Note(parsing: "Z"), preservingSelection: false)
        XCTAssertFalse(window.firstResponder is BlockTextView)
    }

    func testSetNoteIsUndoable() throws {
        try open("mine")
        editor.setNote(try Note(parsing: "theirs"), preservingSelection: true)
        window.undoManager?.undo()
        XCTAssertEqual(firstPageBlocks, [.text("mine")])
    }

    func testLongNotesOnlyCreateViewsNearTheViewport() throws {
        let body = (1...50).map { "# Page \($0)\n" + String(repeating: "Some text that fills the page.\n\n", count: 20) }
            .joined(separator: "+++page\n")
        try open(body)
        let realized = editor.pageViews.filter(\.isRealized).count
        XCTAssertLessThan(realized, 6)
        XCTAssertLessThan(editor.textViews.count, 6)

        var visible: [Int] = []
        editor.onVisiblePageChange = { visible.append($0) }
        editor.scrollToPage(40)
        XCTAssertTrue(editor.pageViews[39].isRealized)
        XCTAssertEqual(visible.last, 40)
        XCTAssertLessThan(editor.pageViews.filter(\.isRealized).count, 12)
        let top = editor.scrollView.contentView.bounds.minY
        XCTAssertEqual(editor.pageViews[39].frame.minY + PageGeometry.shadowInset - PageGeometry.pageGap, top, accuracy: 1)
    }

    func testPagesFollowTheWindowWidth() throws {
        try open("Text")
        let wide = try XCTUnwrap(editor.textViews[block(0, 0).id]).frame.width
        XCTAssertEqual(wide, PageGeometry.maxTextWidth)
        window.setContentSize(NSSize(width: 500, height: 600))
        editor.layoutSubtreeIfNeeded()
        let narrow = try XCTUnwrap(editor.textViews[block(0, 0).id]).frame.width
        XCTAssertLessThan(narrow, 420)
        XCTAssertLessThanOrEqual(editor.documentView.frame.width, editor.scrollView.contentSize.width)
    }

    func testEmptyPageShowsThePlaceholder() throws {
        try open("")
        XCTAssertTrue(try XCTUnwrap(editor.textViews[block(0, 0).id]).isAlonePlaceholder)
    }
}
