import Foundation
import WhiteprintCore

/// One study run through a deck: due cards first (most overdue first), then
/// new ones. A card rated “Again” goes back to the end of the queue.
struct FlashcardSession {
    struct Item: Equatable {
        /// The card's progress key (see `key(for:)`).
        var key: String
        var card: Flashcard
    }

    struct Summary: Equatable, CustomStringConvertible {
        var due: Int
        var new: Int

        var description: String { "\(due) due · \(new) new" }
    }

    struct Stats: Equatable {
        /// Different cards answered at least once.
        var cards = 0
        var answers = 0
        var ratings: [CardRating: Int] = [:]
    }

    private(set) var queue: [Item]
    private(set) var isFlipped = false
    private(set) var progress: [String: CardProgress]
    private(set) var stats = Stats()
    private var answered: Set<String> = []

    /// With `everything`, every card is queued, not only due and new ones
    /// (for studying ahead when nothing is due).
    init(deck: CardDeck, progress: [String: CardProgress], now: Date, shuffled: Bool = false, everything: Bool = false) {
        self.progress = progress
        var seen = Set<String>()
        let items = deck.cards.compactMap { card -> Item? in
            let key = Self.key(for: card)
            return seen.insert(key).inserted ? Item(key: key, card: card) : nil
        }
        var due = items.filter { FlashcardScheduler.isDue(progress[$0.key], now: now) }
            .sorted { progress[$0.key]!.due < progress[$1.key]!.due }
        var new = items.filter { progress[$0.key] == nil }
        if everything {
            let queued = Set((due + new).map(\.key))
            due += items.filter { !queued.contains($0.key) }
        }
        if shuffled {
            due.shuffle()
            new.shuffle()
        }
        queue = due + new
    }

    var current: Item? { queue.first }
    var isFinished: Bool { queue.isEmpty }

    mutating func flip() {
        guard current != nil else { return }
        isFlipped.toggle()
    }

    /// Rates the current card and moves on. Returns its new progress, to be saved.
    @discardableResult
    mutating func rate(_ rating: CardRating, now: Date) -> (key: String, progress: CardProgress)? {
        guard let item = current else { return nil }
        let next = FlashcardScheduler.next(after: progress[item.key], rating: rating, now: now)
        progress[item.key] = next
        queue.removeFirst()
        if rating == .again { queue.append(item) }
        isFlipped = false
        if answered.insert(item.key).inserted { stats.cards += 1 }
        stats.answers += 1
        stats.ratings[rating, default: 0] += 1
        return (item.key, next)
    }

    static func summary(of deck: CardDeck, progress: [String: CardProgress], now: Date) -> Summary {
        let keys = Set(deck.cards.map(key(for:)))
        return Summary(
            due: keys.filter { FlashcardScheduler.isDue(progress[$0], now: now) }.count,
            new: keys.filter { progress[$0] == nil }.count
        )
    }

    /// A stable key for a card: a hash of its question, whitespace-normalized,
    /// so editing the answer keeps the card's history.
    static func key(for card: Flashcard) -> String {
        let question = card.question.split(whereSeparator: \.isWhitespace).joined(separator: " ")
        return StableHash.hex(question)
    }
}

/// FNV-1a, 64 bit: the same on every run (unlike `Hasher`).
enum StableHash {
    static func hex(_ text: String) -> String {
        var hash: UInt64 = 0xcbf2_9ce4_8422_2325
        for byte in text.utf8 {
            hash ^= UInt64(byte)
            hash = hash &* 0x100_0000_01b3
        }
        return String(hash, radix: 16).leftPadded(to: 16)
    }
}

private extension String {
    func leftPadded(to length: Int) -> String {
        count >= length ? self : String(repeating: "0", count: length - count) + self
    }
}
