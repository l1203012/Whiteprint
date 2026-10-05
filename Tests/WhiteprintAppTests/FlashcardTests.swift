import Foundation
import XCTest
@testable import WhiteprintApp
import WhiteprintCore

final class FlashcardSchedulerTests: XCTestCase {
    private let now = Date(timeIntervalSince1970: 1_800_000_000)
    private let day: TimeInterval = 86_400

    func testNewCardIntervals() {
        XCTAssertEqual(FlashcardScheduler.next(after: nil, rating: .again, now: now).due, now.addingTimeInterval(600))
        XCTAssertEqual(FlashcardScheduler.next(after: nil, rating: .hard, now: now).interval, 1)
        XCTAssertEqual(FlashcardScheduler.next(after: nil, rating: .good, now: now).interval, 1)
        let easy = FlashcardScheduler.next(after: nil, rating: .easy, now: now)
        XCTAssertEqual(easy.interval, 4)
        XCTAssertEqual(easy.due, now.addingTimeInterval(4 * day))
        XCTAssertEqual(easy.ease, 2.65, accuracy: 0.001)
    }

    func testGoodAnswersGrowTheInterval() {
        var card = FlashcardScheduler.next(after: nil, rating: .good, now: now)
        card = FlashcardScheduler.next(after: card, rating: .good, now: now)
        XCTAssertEqual(card.interval, 3)
        card = FlashcardScheduler.next(after: card, rating: .good, now: now)
        XCTAssertEqual(card.interval, 7.5, accuracy: 0.01)
        XCTAssertEqual(card.reps, 3)
        XCTAssertEqual(card.ease, 2.5, accuracy: 0.001)
    }

    func testLapseResetsAndLowersEase() {
        var card = FlashcardScheduler.next(after: nil, rating: .good, now: now)
        card = FlashcardScheduler.next(after: card, rating: .good, now: now)
        card = FlashcardScheduler.next(after: card, rating: .again, now: now)
        XCTAssertEqual(card.reps, 0)
        XCTAssertEqual(card.lapses, 1)
        XCTAssertEqual(card.interval, 0)
        XCTAssertEqual(card.ease, 2.3, accuracy: 0.001)
        for _ in 0..<20 { card = FlashcardScheduler.next(after: card, rating: .again, now: now) }
        XCTAssertEqual(card.ease, FlashcardScheduler.minimumEase, accuracy: 0.001)
    }

    func testDueAndLabels() {
        let card = FlashcardScheduler.next(after: nil, rating: .good, now: now)
        XCTAssertFalse(FlashcardScheduler.isDue(card, now: now))
        XCTAssertTrue(FlashcardScheduler.isDue(card, now: now.addingTimeInterval(day)))
        XCTAssertFalse(FlashcardScheduler.isDue(nil, now: now))
        XCTAssertEqual(FlashcardScheduler.intervalLabel(after: nil, rating: .again, now: now), "10m")
        XCTAssertEqual(FlashcardScheduler.intervalLabel(after: nil, rating: .good, now: now), "1d")
        XCTAssertEqual(FlashcardScheduler.intervalLabel(after: nil, rating: .easy, now: now), "4d")
    }
}

final class FlashcardSessionTests: XCTestCase {
    private let now = Date(timeIntervalSince1970: 1_800_000_000)
    private let deck = CardDeck(id: "c1", title: "TCP", cards: [
        Flashcard(question: "Q1", answer: "A1"),
        Flashcard(question: "Q2", answer: "A2"),
        Flashcard(question: "Q3", answer: "A3"),
        Flashcard(question: "Q1", answer: "duplicate"),
    ])

    private func key(_ question: String) -> String {
        FlashcardSession.key(for: Flashcard(question: question, answer: ""))
    }

    func testQueuesDueBeforeNewAndSkipsLaterCards() {
        let progress = [
            key("Q2"): CardProgress(interval: 1, ease: 2.5, reps: 1, lapses: 0, due: now.addingTimeInterval(-60)),
            key("Q3"): CardProgress(interval: 5, ease: 2.5, reps: 2, lapses: 0, due: now.addingTimeInterval(3600)),
        ]
        let session = FlashcardSession(deck: deck, progress: progress, now: now)
        XCTAssertEqual(session.queue.map(\.card.question), ["Q2", "Q1"])
        XCTAssertEqual(FlashcardSession.summary(of: deck, progress: progress, now: now), .init(due: 1, new: 1))
        XCTAssertEqual(FlashcardSession.summary(of: deck, progress: progress, now: now).description, "1 due · 1 new")
        let everything = FlashcardSession(deck: deck, progress: progress, now: now, everything: true)
        XCTAssertEqual(everything.queue.map(\.card.question), ["Q2", "Q3", "Q1"])
    }

    func testFlipRateAndRequeueOnAgain() throws {
        var session = FlashcardSession(deck: deck, progress: [:], now: now)
        XCTAssertEqual(session.current?.card.question, "Q1")
        session.flip()
        XCTAssertTrue(session.isFlipped)
        let saved = try XCTUnwrap(session.rate(.again, now: now))
        XCTAssertEqual(saved.key, key("Q1"))
        XCTAssertFalse(session.isFlipped)
        XCTAssertEqual(session.queue.map(\.card.question), ["Q2", "Q3", "Q1"])
        session.rate(.good, now: now)
        session.rate(.good, now: now)
        session.rate(.easy, now: now)
        XCTAssertTrue(session.isFinished)
        XCTAssertEqual(session.stats.cards, 3)
        XCTAssertEqual(session.stats.answers, 4)
        XCTAssertEqual(session.stats.ratings, [.good: 2, .again: 1, .easy: 1])
        XCTAssertEqual(session.progress.count, 3)
    }

    func testShuffleKeepsTheCards() {
        let session = FlashcardSession(deck: deck, progress: [:], now: now, shuffled: true)
        XCTAssertEqual(Set(session.queue.map(\.card.question)), ["Q1", "Q2", "Q3"])
    }

    func testKeysAreStableAndIgnoreSpacing() {
        XCTAssertEqual(StableHash.hex(""), "cbf29ce484222325")
        XCTAssertEqual(StableHash.hex("a"), "af63dc4c8601ec8c")
        XCTAssertEqual(key("What  is\nTCP?"), key("What is TCP?"))
        XCTAssertNotEqual(key("Q1"), key("Q2"))
    }

    func testDeckCatalog() {
        let note = NoteEntry(url: URL(fileURLWithPath: "/notes/TCP.wprint"), title: "Networks", pageCount: 1, folder: "",
                             decks: [deck, CardDeck(id: "c2", title: nil, cards: []), CardDeck(id: "c3", title: " ", cards: [Flashcard(question: "Q", answer: "A")])])
        let entries = DeckCatalog.entries(in: [note], progress: { _, _ in [:] }, now: now)
        XCTAssertEqual(entries.map(\.deck.id), ["c1", "c3"])
        XCTAssertEqual(entries.map(\.title), ["TCP", "Networks"])
        XCTAssertEqual(entries.first?.summary, .init(due: 0, new: 3))
    }
}

final class FlashcardProgressStoreTests: XCTestCase {
    private var directory: URL!

    override func setUpWithError() throws {
        directory = FileManager.default.temporaryDirectory.appendingPathComponent("wp-cards-\(UUID().uuidString)", isDirectory: true)
    }

    override func tearDown() {
        try? FileManager.default.removeItem(at: directory)
    }

    func testSavesPerNoteDeckAndCard() throws {
        let note = URL(fileURLWithPath: "/notes/TCP.wprint")
        let progress = CardProgress(interval: 3, ease: 2.5, reps: 2, lapses: 0, due: Date(timeIntervalSince1970: 1_800_000_000))
        let store = FlashcardProgressStore(directory: directory)
        try store.save(progress, card: "k1", note: note, deck: "c1")
        XCTAssertEqual(FlashcardProgressStore(directory: directory).progress(note: note, deck: "c1"), ["k1": progress])
        XCTAssertEqual(store.progress(note: note, deck: "c2"), [:])
        XCTAssertEqual(store.progress(note: URL(fileURLWithPath: "/notes/Other.wprint"), deck: "c1"), [:])
    }

    func testProgressFollowsMovedNotesAndFolders() throws {
        let note = URL(fileURLWithPath: "/notes/Courses/TCP.wprint")
        let progress = CardProgress(interval: 1, ease: 2.5, reps: 1, lapses: 0, due: Date(timeIntervalSince1970: 1_800_000_000))
        let store = FlashcardProgressStore(directory: directory)
        try store.save(progress, card: "k1", note: note, deck: "c1")
        store.moveNotes(from: URL(fileURLWithPath: "/notes/Courses"), to: URL(fileURLWithPath: "/notes/Archive"))
        XCTAssertEqual(store.progress(note: note, deck: "c1"), [:])
        let moved = URL(fileURLWithPath: "/notes/Archive/TCP.wprint")
        XCTAssertEqual(FlashcardProgressStore(directory: directory).progress(note: moved, deck: "c1"), ["k1": progress])
        XCTAssertEqual(try FileManager.default.contentsOfDirectory(atPath: directory.path).count, 1)
    }
}
