#pragma once
// Port of FlashcardSession.swift.
#include "app/FlashcardScheduler.h"
#include "core/CardDeck.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <map>
#include <optional>

namespace wp {

/// One study run through a deck: due cards first (most overdue first), then new ones. A card rated
/// "Again" goes back to the end of the queue.
class FlashcardSession {
public:
    struct Item {
        /// The card's progress key (see `key`).
        QString key;
        Flashcard card;
        bool operator==(const Item &) const = default;
    };

    struct Summary {
        int due = 0;
        /// Cards without progress.
        int newCount = 0;
        bool operator==(const Summary &) const = default;
        /// `3 due · 1 new`.
        QString description() const;
    };

    struct Stats {
        /// Different cards answered at least once.
        int cards = 0;
        int answers = 0;
        std::map<CardRating, int> ratings;
        bool operator==(const Stats &) const = default;
    };

    /// What `rate` returns: the card's key and its new progress, to be saved.
    struct Rated {
        QString key;
        CardProgress progress;
    };

    /// With `everything`, every card is queued, not only due and new ones (for studying ahead when
    /// nothing is due). Duplicate questions count once.
    FlashcardSession(const CardDeck &deck, const QHash<QString, CardProgress> &progress, const QDateTime &now,
                     bool shuffled = false, bool everything = false);

    const QList<Item> &queue() const { return m_queue; }
    bool isFlipped() const { return m_isFlipped; }
    const QHash<QString, CardProgress> &progress() const { return m_progress; }
    const Stats &stats() const { return m_stats; }

    /// The card being asked, nullptr when the session is finished.
    const Item *current() const { return m_queue.isEmpty() ? nullptr : &m_queue.first(); }
    bool isFinished() const { return m_queue.isEmpty(); }

    void flip();

    /// Rates the current card and moves on. Returns its new progress, to be saved; nullopt when finished.
    std::optional<Rated> rate(CardRating rating, const QDateTime &now);

    static Summary summary(const CardDeck &deck, const QHash<QString, CardProgress> &progress, const QDateTime &now);

    /// A stable key for a card: a hash of its question, whitespace-normalized, so editing the answer
    /// keeps the card's history.
    static QString key(const Flashcard &card);

private:
    QList<Item> m_queue;
    bool m_isFlipped = false;
    QHash<QString, CardProgress> m_progress;
    Stats m_stats;
    QSet<QString> m_answered;
};

/// FNV-1a, 64 bit: the same on every run.
namespace StableHash {
/// 16 lowercase hex digits.
QString hex(const QString &text);
} // namespace StableHash

} // namespace wp
