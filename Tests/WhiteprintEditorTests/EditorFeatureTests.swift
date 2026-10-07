import AppKit
import XCTest
import WhiteprintCore
import WhiteprintRender
@testable import WhiteprintEditor

/// Layout modes, concealed Markdown, commands, the Touch Bar and decks.
final class EditorFeatureTests: XCTestCase {
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

    private func open(_ text: String, height: CGFloat = 800) throws {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 900, height: height), styleMask: [.titled, .resizable],
                          backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false
        editor = NoteEditorView(note: try Note(parsing: text))
        window.contentView = editor
        editor.layoutSubtreeIfNeeded()
    }

    private func block(_ page: Int, _ index: Int) -> EditorBlock {
        editor.document.pages[page].blocks[index]
    }

    @discardableResult
    private func focusText(_ page: Int, _ index: Int, caret: Int, length: Int = 0) throws -> BlockTextView {
        editor.focus(.text(block(page, index).id, NSRange(location: caret, length: length)))
        return try XCTUnwrap(window.firstResponder as? BlockTextView)
    }

    private func press(keyCode: UInt16, characters: String) throws {
        let responder = try XCTUnwrap(window.firstResponder as? NSView)
        let event = try XCTUnwrap(NSEvent.keyEvent(
            with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0, windowNumber: window.windowNumber,
            context: nil, characters: characters, charactersIgnoringModifiers: characters, isARepeat: false, keyCode: keyCode))
        responder.keyDown(with: event)
    }

    private var firstPageBlocks: [NoteBlock] { editor.note.pages[0].blocks }

    private let deckNote = "Intro\n\n```cards id=c1\n# Deck\nQ: q1\nA: a1\n```\n\nOutro"

    // MARK: Layout modes

    func testA4UsesPaperSheetsAndScaledText() throws {
        try open("Text")
        let view = try XCTUnwrap(editor.textViews[block(0, 0).id])
        XCTAssertEqual(view.fontSize, 15)
        editor.layoutMode = .a4
        let geometry = editor.geometry
        XCTAssertEqual(view.frame.width, geometry.textWidth)
        XCTAssertEqual(view.fontSize, geometry.fontSize)
        XCTAssertEqual((view.textStorage?.attribute(.font, at: 0, effectiveRange: nil) as? NSFont)?.pointSize, geometry.fontSize)
        XCTAssertEqual(editor.pageViews[0].sheetRect.height, geometry.minPageHeight, accuracy: 1)
        editor.layoutMode = .slides
        XCTAssertEqual(view.fontSize, 15)
        XCTAssertEqual(view.frame.width, PageGeometry.maxTextWidth)
    }

    func testA4MarksPrintedPageBreaks() throws {
        let long = (1...80).map { "Line \($0) of a long page." }.joined(separator: "\n\n")
        try open(long)
        XCTAssertEqual(editor.pageViews[0].pageBreaks, [])
        editor.layoutMode = .a4
        XCTAssertGreaterThanOrEqual(editor.pageViews[0].pageBreaks.count, 1)
        XCTAssertGreaterThan(editor.pageViews[0].sheetRect.height, editor.geometry.minPageHeight)
    }

    func testSwitchingModesKeepsTheTopBlock() throws {
        let body = (1...30).map { "# Page \($0)\n" + String(repeating: "Some text that fills the page.\n\n", count: 12) }
            .joined(separator: "+++page\n")
        try open(body)
        editor.scrollToPage(12)
        editor.scroll(toY: editor.scrollView.contentView.bounds.minY + 150)
        let top = try XCTUnwrap(editor.topBlock())
        editor.layoutMode = .a4
        XCTAssertEqual(editor.topBlock()?.id, top.id)
        editor.layoutMode = .slides
        XCTAssertEqual(editor.topBlock()?.id, top.id)
        XCTAssertEqual(editor.topBlock()?.fraction ?? -1, top.fraction, accuracy: 0.05)
    }

    // MARK: Concealed Markdown

    func testHidingSyntaxConcealsAllButTheCaretParagraph() throws {
        try open("# Title\n**bold** and `code`\n- [ ] task")
        let view = try focusText(0, 0, caret: 3)
        editor.showsMarkdownSyntax = false
        let storage = try XCTUnwrap(view.textStorage)
        func markup(at index: Int) -> String? {
            storage.attribute(MarkdownStyler.markupKey, at: index, effectiveRange: nil) as? String
        }
        XCTAssertNil(markup(at: 0), "the caret's line shows its markup")
        XCTAssertEqual(markup(at: 8), "hidden")
        XCTAssertEqual(markup(at: 28), "hidden", "the task's dash")
        XCTAssertEqual(markup(at: 30), "checkbox")

        view.setSelectedRange(NSRange(location: 10, length: 0))
        XCTAssertEqual(markup(at: 0), "hidden")
        XCTAssertNil(markup(at: 8))
        XCTAssertEqual(view.revealedRange, NSRange(location: 8, length: 20))

        window.makeFirstResponder(nil)
        XCTAssertNil(view.revealedRange)
        XCTAssertEqual(markup(at: 8), "hidden")
        editor.showsMarkdownSyntax = true
        XCTAssertNil(markup(at: 0))
        XCTAssertEqual(view.string, "# Title\n**bold** and `code`\n- [ ] task", "the text stays raw Markdown")
    }

    func testChangingThePaletteRestylesInPlace() throws {
        try open("Intro text")
        let view = try XCTUnwrap(editor.textViews[block(0, 0).id])
        XCTAssertFalse(editor.palette.showsGrid, "pages default to the Paper theme")
        editor.palette = .blueprint
        let font = try XCTUnwrap(view.textStorage?.attribute(.font, at: 0, effectiveRange: nil) as? NSFont)
        XCTAssertEqual(font, NSFont.systemFont(ofSize: font.pointSize))
        XCTAssertEqual(view.textStorage?.attribute(.foregroundColor, at: 0, effectiveRange: nil) as? NSColor, .white)
        XCTAssertEqual(editor.scrollView.backgroundColor, BlueprintPalette.blueprint.canvas)
    }

    func testConcealedGlyphsTakeNoSpace() throws {
        try open("Intro\n# Title\n- item")
        editor.showsMarkdownSyntax = false
        let view = try XCTUnwrap(editor.textViews[block(0, 0).id])
        let layout = try XCTUnwrap(view.layoutManager)
        layout.ensureLayout(for: try XCTUnwrap(view.textContainer))
        let hash = layout.glyphIndexForCharacter(at: 6)
        XCTAssertEqual(layout.propertyForGlyph(at: hash), .controlCharacter)
        XCTAssertEqual(layout.location(forGlyphAt: layout.glyphIndexForCharacter(at: 8)).x, 0, accuracy: 0.5,
                       "the heading starts at the margin")
        XCTAssertEqual(layout.location(forGlyphAt: layout.glyphIndexForCharacter(at: 14)).x, 0, accuracy: 0.5)
        let bullet = layout.glyphIndexForCharacter(at: 14)
        XCTAssertEqual(layout.cgGlyph(at: bullet), ConcealedGlyphs.bulletGlyph(in: .systemFont(ofSize: 15)))
    }

    func testTypingAndCopyingWhileConcealed() throws {
        try open("**a** b\n**c**")
        editor.showsMarkdownSyntax = false
        let view = try focusText(0, 0, caret: 7)
        view.insertText("!", replacementRange: view.selectedRange())
        XCTAssertEqual(firstPageBlocks, [.text("**a** b!\n**c**")])
        view.setSelectedRange(NSRange(location: 0, length: 14))
        let pasteboard = NSPasteboard(name: NSPasteboard.Name("whiteprint-editor-tests-\(UUID().uuidString)"))
        defer { pasteboard.releaseGlobally() }
        XCTAssertTrue(view.writeSelection(to: pasteboard, types: view.writablePasteboardTypes))
        XCTAssertEqual(pasteboard.string(forType: .string), "**a** b!\n**c**")
    }

    func testConcealedCheckboxStillToggles() throws {
        try open("Intro\n- [ ] task")
        editor.showsMarkdownSyntax = false
        let view = try XCTUnwrap(editor.textViews[block(0, 0).id])
        let box = view.rect(for: NSRange(location: 8, length: 3))
        XCTAssertEqual(view.checkbox(at: NSPoint(x: box.midX, y: box.midY)), NSRange(location: 8, length: 3))
        editor.blockTextView(view, toggleCheckboxAt: 8)
        XCTAssertEqual(firstPageBlocks, [.text("Intro\n- [x] task")])
    }

    // MARK: Commands

    func testPerformFormatsTheSelectionUndoably() throws {
        try open("Title\nword")
        try focusText(0, 0, caret: 6, length: 4)
        editor.perform(.bold)
        XCTAssertEqual(firstPageBlocks, [.text("Title\n**word**")])
        XCTAssertEqual(window.undoManager?.undoActionName, "Bold")
        window.undoManager?.undo()
        XCTAssertEqual(firstPageBlocks, [.text("Title\nword")])
        try focusText(0, 0, caret: 2)
        editor.perform(.heading1)
        XCTAssertEqual(firstPageBlocks, [.text("# Title\nword")])
    }

    func testPerformInsertCommands() throws {
        try open("One")
        try focusText(0, 0, caret: 3)
        editor.perform(.newPage)
        XCTAssertEqual(editor.note.pages.count, 2)
        editor.perform(.flashcards)
        XCTAssertEqual(editor.note.decks.map(\.id), ["c1"])
        XCTAssertNotNil(editor.deckEditor)
        editor.deckEditor?.commit()
        editor.perform(.drawing)
        XCTAssertEqual(editor.note.drawings.map(\.id), ["d1"])
        editor.drawingEditor?.commit()
    }

    func testTextCommandsNeedATextBlock() throws {
        try open(deckNote)
        editor.focus(.block(block(0, 1).id))
        editor.perform(.bold)
        XCTAssertEqual(editor.note, try Note(parsing: deckNote))
    }

    // MARK: Touch Bar

    func testTouchBarItemsRunCommands() throws {
        try open("word")
        let view = try focusText(0, 0, caret: 0, length: 4)
        let bar = try XCTUnwrap(view.makeTouchBar())
        XCTAssertEqual(bar.customizationIdentifier, "io.github.l1203012.whiteprint.editor")
        XCTAssertEqual(bar.defaultItemIdentifiers,
                       [EditorTouchBar.inline, EditorTouchBar.headings, EditorTouchBar.lists, EditorTouchBar.more])
        XCTAssertTrue(Set(bar.defaultItemIdentifiers).isSubset(of: Set(bar.customizationAllowedItemIdentifiers)))

        /// Taps a segment of one of the bar's groups.
        func tap(_ group: NSTouchBarItem.Identifier, _ segment: Int) throws {
            let item = try XCTUnwrap(bar.item(forIdentifier: group) as? NSCustomTouchBarItem, group.rawValue)
            XCTAssertEqual(item.visibilityPriority, .high)
            let control = try XCTUnwrap(item.view as? NSSegmentedControl)
            XCTAssertEqual(control.trackingMode, .momentary)
            // A momentary control drops a selection set in code; a tap
            // leaves it set while the action runs.
            control.trackingMode = .selectOne
            defer { control.trackingMode = .momentary }
            control.selectedSegment = segment
            NSApp.sendAction(try XCTUnwrap(control.action), to: control.target, from: control)
        }
        let inline = try XCTUnwrap((bar.item(forIdentifier: EditorTouchBar.inline) as? NSCustomTouchBarItem)?.view as? NSSegmentedControl)
        XCTAssertEqual(inline.segmentCount, 3)
        XCTAssertNotNil(inline.image(forSegment: 0))
        XCTAssertEqual(inline.toolTip(forSegment: 0), "Bold")
        try tap(EditorTouchBar.inline, 0)
        XCTAssertEqual(firstPageBlocks, [.text("**word**")])

        let headings = try XCTUnwrap((bar.item(forIdentifier: EditorTouchBar.headings) as? NSCustomTouchBarItem)?.view as? NSSegmentedControl)
        XCTAssertEqual((0..<3).map { headings.label(forSegment: $0) }, ["H1", "H2", "H3"])
        try tap(EditorTouchBar.headings, 1)
        XCTAssertEqual(firstPageBlocks, [.text("## **word**")])
        try tap(EditorTouchBar.lists, 2)
        XCTAssertEqual(firstPageBlocks, [.text("- [ ] **word**")])

        let more = try XCTUnwrap(bar.item(forIdentifier: EditorTouchBar.more) as? NSPopoverTouchBarItem)
        let popover = try XCTUnwrap(more.popoverTouchBar)
        XCTAssertEqual(popover.defaultItemIdentifiers, EditorTouchBar.moreCommands.map(EditorTouchBar.identifier(for:)))
        let body = try XCTUnwrap(popover.item(forIdentifier: EditorTouchBar.identifier(for: .body)) as? NSButtonTouchBarItem)
        XCTAssertEqual(body.title, "Body")
        XCTAssertNil(body.image)
        NSApp.sendAction(try XCTUnwrap(body.action), to: body.target, from: body)
        XCTAssertEqual(firstPageBlocks, [.text("**word**")])

        // Every command can be added to the bar as a button of its own.
        for command in EditorCommand.allCases {
            let id = EditorTouchBar.identifier(for: command)
            XCTAssertTrue(bar.customizationAllowedItemIdentifiers.contains(id), command.rawValue)
            let button = try XCTUnwrap(EditorTouchBar(onCommand: { _ in }).touchBar(bar, makeItemForIdentifier: id) as? NSButtonTouchBarItem)
            XCTAssertEqual(button.customizationLabel, command.title)
        }
    }

    // MARK: Decks

    func testDeckBlockShowsTheDeck() throws {
        try open(deckNote)
        let deckView = try XCTUnwrap(editor.deckViews[block(0, 1).id])
        XCTAssertEqual(deckView.deck.title, "Deck")
        let closed = deckView.frame.height
        deckView.toggleAnswer(0)
        editor.layoutSubtreeIfNeeded()
        XCTAssertGreaterThan(deckView.frame.height, closed)
        XCTAssertEqual(deckView.revealed, [0])
    }

    func testStudyButtonCallsBack() throws {
        try open(deckNote)
        var studied: [CardDeck] = []
        editor.onStudyDeck = { studied.append($0) }
        editor.deckBlockViewRequestsStudy(try XCTUnwrap(editor.deckViews[block(0, 1).id]))
        XCTAssertEqual(studied.map(\.id), ["c1"])
    }

    func testEditingADeckIsUndoable() throws {
        try open(deckNote)
        let id = block(0, 1).id
        editor.editDeck(id)
        let deckEditor = try XCTUnwrap(editor.deckEditor)
        deckEditor.loadView()
        deckEditor.addCard()
        editor.commitDeck(id, CardDeck(title: "New", cards: [Flashcard(question: "q2", answer: "a2", ref: "r")]))
        XCTAssertEqual(editor.note.decks, [CardDeck(id: "c1", title: "New", cards: [Flashcard(question: "q2", answer: "a2", ref: "r")])])
        XCTAssertEqual(editor.deckViews[id]?.deck.title, "New")
        window.undoManager?.undo()
        XCTAssertEqual(editor.note.decks.first?.title, "Deck")
    }

    func testInsertDeckSplitsTheTextAndIDsAreNeverReused() throws {
        try open("Before after")
        try focusText(0, 0, caret: 7)
        editor.insertDeckAtSelection()
        XCTAssertEqual(firstPageBlocks, [.text("Before "), .cards(CardDeck(id: "c1", cards: [])), .text("after")])
        editor.closeDeckEditor(commit: true)
        editor.deleteBlock(block(0, 1).id)
        XCTAssertEqual(editor.note.frontMatter["last-cards"], "1")
        window.undoManager?.undo()
        window.undoManager?.undo()
        XCTAssertEqual(editor.note.frontMatter["last-cards"], "1", "undo doesn't give the id back")
        try focusText(0, 0, caret: 0)
        editor.insertDeckAtSelection()
        XCTAssertEqual(editor.note.decks.map(\.id), ["c2"])
    }

    func testSlashFlashcards() throws {
        try open("")
        let view = try focusText(0, 0, caret: 0)
        for character in "/flash" { view.insertText(String(character), replacementRange: view.selectedRange()) }
        XCTAssertEqual(editor.slashMenu?.state.selected, .flashcards)
        editor.chooseSlashCommand(at: nil)
        XCTAssertEqual(editor.note.decks.count, 1)
        XCTAssertNotNil(editor.deckEditor)
    }

    func testArrowsBackspaceAndDeleteOnADeck() throws {
        try open(deckNote)
        try focusText(0, 0, caret: 5)
        try press(keyCode: 125, characters: String(UnicodeScalar(UInt16(NSDownArrowFunctionKey))!))
        XCTAssertTrue(window.firstResponder is DeckBlockView)
        try press(keyCode: 125, characters: String(UnicodeScalar(UInt16(NSDownArrowFunctionKey))!))
        XCTAssertEqual((window.firstResponder as? BlockTextView)?.string, "Outro")
        editor.focus(.text(block(0, 2).id, NSRange(location: 0, length: 0)))
        try press(keyCode: 51, characters: "\u{7f}")
        XCTAssertTrue(window.firstResponder is DeckBlockView)
        try press(keyCode: 51, characters: "\u{7f}")
        XCTAssertEqual(firstPageBlocks, [.text("Intro"), .text("Outro")])
        window.undoManager?.undo()
        XCTAssertEqual(editor.note.decks.map(\.id), ["c1"])
    }

    func testDuplicateAndMoveADeck() throws {
        try open(deckNote)
        let id = block(0, 1).id
        editor.duplicateBlock(id)
        XCTAssertEqual(editor.note.decks.map(\.id), ["c1", "c2"])
        editor.moveBlock(id, by: -1)
        guard case .cards(let first) = firstPageBlocks[0] else { return XCTFail("deck should move up") }
        XCTAssertEqual(first.id, "c1")
    }

    func testSetNoteKeepsTheDeckView() throws {
        try open(deckNote)
        let deckView = editor.deckViews[block(0, 1).id]
        let changed = try Note(parsing: "Intro!\n\n```cards id=c1\nQ: new\nA: card\n```\n\nOutro")
        editor.setNote(changed, preservingSelection: true)
        XCTAssertTrue(editor.deckViews[block(0, 1).id] === deckView)
        XCTAssertEqual(deckView?.deck.cards.first?.question, "new")
    }

    func testDeckEditorClosesWhenItsDeckDisappears() throws {
        try open(deckNote)
        editor.editDeck(block(0, 1).id)
        XCTAssertNotNil(editor.deckEditor)
        editor.setNote(try Note(parsing: "Intro"), preservingSelection: true)
        XCTAssertNil(editor.deckEditor)
        XCTAssertEqual(editor.note.decks, [])
    }
}
