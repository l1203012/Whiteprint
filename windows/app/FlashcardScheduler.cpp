#include "app/FlashcardScheduler.h"

#include <algorithm>
#include <cmath>

namespace wp {

QString cardRatingTitle(CardRating rating)
{
    switch (rating) {
    case CardRating::again: return QStringLiteral("Again");
    case CardRating::hard: return QStringLiteral("Hard");
    case CardRating::good: return QStringLiteral("Good");
    case CardRating::easy: return QStringLiteral("Easy");
    }
    return {};
}

namespace FlashcardScheduler {

namespace {
constexpr double day = 24 * 60 * 60;

QDateTime adding(const QDateTime &time, double seconds)
{
    return time.addMSecs(qint64(std::llround(seconds * 1000)));
}
} // namespace

CardProgress next(const std::optional<CardProgress> &after, CardRating rating, const QDateTime &now)
{
    CardProgress card = after.value_or(CardProgress{0, startingEase, 0, 0, now});
    switch (rating) {
    case CardRating::again:
        if (after && card.reps > 0)
            card.lapses += 1;
        card.reps = 0;
        card.interval = 0;
        card.ease = std::max(minimumEase, card.ease - 0.2);
        card.due = adding(now, relearnDelay);
        return card;
    case CardRating::hard:
        card.interval = card.reps == 0 ? 1 : std::max(1.0, card.interval * 1.2);
        card.ease = std::max(minimumEase, card.ease - 0.15);
        break;
    case CardRating::good:
        card.interval = card.reps == 0 ? 1 : card.reps == 1 ? std::max(3.0, card.interval * 1.5) : card.interval * card.ease;
        break;
    case CardRating::easy:
        card.interval = card.reps == 0 ? 4 : std::max(4.0, card.interval * card.ease * 1.3);
        card.ease += 0.15;
        break;
    }
    card.interval = std::round(card.interval * 10) / 10;
    card.reps += 1;
    card.due = adding(now, card.interval * day);
    return card;
}

bool isDue(const std::optional<CardProgress> &progress, const QDateTime &now)
{
    return progress && progress->due <= now;
}

QString intervalLabel(const std::optional<CardProgress> &after, CardRating rating, const QDateTime &now)
{
    const double seconds = double(now.msecsTo(next(after, rating, now).due)) / 1000.0;
    if (seconds < 60 * 60)
        return QStringLiteral("%1m").arg(std::max<long>(1, std::lround(seconds / 60)));
    const double days = seconds / day;
    if (days < 1)
        return QStringLiteral("%1h").arg(std::lround(seconds / 3600));
    if (days < 30)
        return QStringLiteral("%1d").arg(std::lround(days));
    if (days < 365)
        return QStringLiteral("%1mo").arg(std::lround(days / 30));
    return QString::number(days / 365, 'f', 1) + QLatin1Char('y');
}

} // namespace FlashcardScheduler

} // namespace wp
