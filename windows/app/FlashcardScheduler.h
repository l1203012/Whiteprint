#pragma once
// Port of FlashcardScheduler.swift.
#include <QDateTime>
#include <QString>
#include <optional>

namespace wp {

/// How well a card was remembered; the value is its key (1-4).
enum class CardRating { again = 1, hard, good, easy };

/// All ratings, in key order.
inline constexpr CardRating allCardRatings[] = {CardRating::again, CardRating::hard, CardRating::good, CardRating::easy};

/// `Again`, `Hard`, `Good`, `Easy`.
QString cardRatingTitle(CardRating rating);

/// A card's review state.
struct CardProgress {
    /// Days until the next review; 0 while relearning.
    double interval = 0;
    double ease = 2.5;
    /// Successful reviews in a row.
    int reps = 0;
    int lapses = 0;
    QDateTime due;

    bool operator==(const CardProgress &) const = default;
};

/// A small SM-2 variant. New and lapsed cards come back after ten minutes ("Again") or move to day
/// intervals; each later success multiplies the interval by the card's ease, which hard and easy
/// answers adjust.
namespace FlashcardScheduler {

inline constexpr double startingEase = 2.5;
inline constexpr double minimumEase = 1.3;
/// Seconds until an "Again" card comes back.
inline constexpr double relearnDelay = 10 * 60;

CardProgress next(const std::optional<CardProgress> &after, CardRating rating, const QDateTime &now);

/// New cards (no progress yet) are not counted as due.
bool isDue(const std::optional<CardProgress> &progress, const QDateTime &now);

/// `10m`, `1d`, `3d`, `2mo` - when the card would come back after `rating`.
QString intervalLabel(const std::optional<CardProgress> &after, CardRating rating, const QDateTime &now);

} // namespace FlashcardScheduler

} // namespace wp
