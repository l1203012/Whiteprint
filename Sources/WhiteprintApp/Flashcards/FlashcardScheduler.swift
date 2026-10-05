import Foundation

/// How well a card was remembered; the raw value is its key (1–4).
enum CardRating: Int, CaseIterable, Codable {
    case again = 1, hard, good, easy

    var title: String {
        switch self {
        case .again: return "Again"
        case .hard: return "Hard"
        case .good: return "Good"
        case .easy: return "Easy"
        }
    }
}

/// A card's review state.
struct CardProgress: Codable, Equatable {
    /// Days until the next review; 0 while relearning.
    var interval: Double
    var ease: Double
    /// Successful reviews in a row.
    var reps: Int
    var lapses: Int
    var due: Date
}

/// A small SM-2 variant. New and lapsed cards come back after ten minutes
/// (“Again”) or move to day intervals; each later success multiplies the
/// interval by the card's ease, which hard and easy answers adjust.
enum FlashcardScheduler {
    static let startingEase = 2.5
    static let minimumEase = 1.3
    static let relearnDelay: TimeInterval = 10 * 60
    private static let day: TimeInterval = 24 * 60 * 60

    static func next(after progress: CardProgress?, rating: CardRating, now: Date) -> CardProgress {
        var card = progress ?? CardProgress(interval: 0, ease: startingEase, reps: 0, lapses: 0, due: now)
        switch rating {
        case .again:
            if progress != nil, card.reps > 0 { card.lapses += 1 }
            card.reps = 0
            card.interval = 0
            card.ease = max(minimumEase, card.ease - 0.2)
            card.due = now.addingTimeInterval(relearnDelay)
            return card
        case .hard:
            card.interval = card.reps == 0 ? 1 : max(1, card.interval * 1.2)
            card.ease = max(minimumEase, card.ease - 0.15)
        case .good:
            card.interval = card.reps == 0 ? 1 : card.reps == 1 ? max(3, card.interval * 1.5) : card.interval * card.ease
        case .easy:
            card.interval = card.reps == 0 ? 4 : max(4, card.interval * card.ease * 1.3)
            card.ease += 0.15
        }
        card.interval = (card.interval * 10).rounded() / 10
        card.reps += 1
        card.due = now.addingTimeInterval(card.interval * day)
        return card
    }

    /// New cards (no progress yet) are not counted as due.
    static func isDue(_ progress: CardProgress?, now: Date) -> Bool {
        progress.map { $0.due <= now } ?? false
    }

    /// `10m`, `1d`, `3d`, `2mo` — when the card would come back after `rating`.
    static func intervalLabel(after progress: CardProgress?, rating: CardRating, now: Date) -> String {
        let seconds = next(after: progress, rating: rating, now: now).due.timeIntervalSince(now)
        if seconds < 60 * 60 { return "\(max(1, Int((seconds / 60).rounded())))m" }
        let days = seconds / day
        if days < 1 { return "\(Int((seconds / 3600).rounded()))h" }
        if days < 30 { return "\(Int(days.rounded()))d" }
        if days < 365 { return "\(Int((days / 30).rounded()))mo" }
        return String(format: "%.1fy", days / 365)
    }
}
