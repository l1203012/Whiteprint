import XCTest
@testable import WhiteprintCore

final class CardDeckTests: XCTestCase {
    func testParsesTitleCardsRefsAndMultiLineFields() {
        let deck = CardDeck(id: "c1", parsing: """
        # TCP basics
        Q: What does TCP guarantee?
        A: Ordered, reliable delivery.
        It retransmits lost segments.
        ref: Lecture3.pptx · slide 4

        Q: Handshake?
        A: SYN, SYN-ACK, ACK
        """)
        XCTAssertEqual(deck.title, "TCP basics")
        XCTAssertEqual(deck.cards, [
            Flashcard(question: "What does TCP guarantee?",
                      answer: "Ordered, reliable delivery.\nIt retransmits lost segments.",
                      ref: "Lecture3.pptx · slide 4"),
            Flashcard(question: "Handshake?", answer: "SYN, SYN-ACK, ACK"),
        ])
    }

    func testSourceRoundTripsAndEscapesStructuralLines() {
        let deck = CardDeck(id: "c1", title: "T", cards: [
            Flashcard(question: "Two\nlines", answer: "A: not a field\n\nQ: nor this\n```\n\\x", ref: "p. 2"),
            Flashcard(question: "Q", answer: ""),
        ])
        let parsed = CardDeck(id: "c1", parsing: deck.source)
        XCTAssertEqual(parsed.cards[0], Flashcard(question: "Two\nlines", answer: "A: not a field\nQ: nor this\n```\n\\x", ref: "p. 2"))
        XCTAssertEqual(parsed.cards[1], Flashcard(question: "Q", answer: ""))
        XCTAssertEqual(parsed.title, "T")
    }

    func testDecksInNotesGetIDsAndRoundTrip() throws {
        let note = try Note(parsing: "Intro\n\n```cards\nQ: a\nA: b\n```\n\n```wp\nbox a\n```")
        XCTAssertEqual(note.decks.map(\.id), ["c1"])
        XCTAssertEqual(note.drawings.map(\.id), ["d1"])
        XCTAssertEqual(note.pages[0].blocks.compactMap(\.id), ["c1", "d1"])
        XCTAssertEqual(try Note(parsing: note.serialized()), note)
        XCTAssertTrue(note.serialized().contains("```cards id=c1\nQ: a\nA: b\n```"))
    }

    func testDeckEditingNeverReusesIDs() throws {
        var note = try Note(parsing: "x")
        let first = try note.insertDeck(CardDeck(cards: [Flashcard(question: "q", answer: "a")]), page: 1)
        let drawing = try note.insertDrawing("box a", page: 1)
        let second = try note.insertDeck(CardDeck(title: "Two", cards: []), page: 1, after: first)
        XCTAssertEqual([first, drawing, second], ["c1", "d1", "c2"])
        XCTAssertEqual(note.pages[0].blocks.compactMap(\.id), ["c1", "c2", "d1"])

        try note.updateDeck("c2", CardDeck(id: "ignored", title: "Renamed", cards: []))
        XCTAssertEqual(note.deck("c2")?.deck.title, "Renamed")
        XCTAssertEqual(note.deck("c2")?.deck.id, "c2")
        try note.deleteDeck("c2")
        XCTAssertNil(note.deck("c2"))
        XCTAssertEqual(try Note(parsing: note.serialized()).nextDeckID(), "c3")
        XCTAssertThrowsError(try note.deleteDeck("c2")) { error in
            XCTAssertEqual((error as? NoteEditError)?.description, "no flashcard deck 'c2' in this note")
        }
    }

    func testReplaceKeepsDeckIDsWrittenBack() throws {
        var note = try Note(parsing: "```cards id=c1\nQ: a\nA: b\n```")
        try note.write("```cards id=c1\nQ: a2\nA: b2\n```", page: 1, mode: .replace)
        XCTAssertEqual(note.decks, [CardDeck(id: "c1", cards: [Flashcard(question: "a2", answer: "b2")])])
    }

    func testMarkdownExportListsCards() throws {
        let note = try Note(parsing: "```cards\n# Deck\nQ: What?\nA: That.\nref: p. 1\n```")
        XCTAssertEqual(note.markdown(), "**Flashcards: Deck**\n\n- **What?**\n  That. *(p. 1)*\n")
    }

    func testTypedStructureInTextIsEscaped() throws {
        let note = Note(pages: [NotePage(blocks: [.text("a\n+++page\n```wp\nbox\n```\n```swift\n+++page\n```")])])
        let reparsed = try Note(parsing: note.serialized())
        XCTAssertEqual(reparsed.pages.count, 1)
        XCTAssertTrue(reparsed.drawings.isEmpty)
        XCTAssertEqual(reparsed.pages[0].blocks, [.text("a\n\\+++page\n\\```wp\nbox\n```\n```swift\n+++page\n```")])
    }
}
