#include "app/FlashcardSession.h"

#include <algorithm>
#include <random>

namespace wp {

namespace StableHash {

QString hex(const QString &text)
{
    quint64 hash = 0xcbf29ce484222325ULL;
    for (const char byte : text.toUtf8()) {
        hash ^= quint64(quint8(byte));
        hash *= 0x100000001b3ULL;
    }
    return QString::number(hash, 16).rightJustified(16, QLatin1Char('0'));
}

} // namespace StableHash

QString FlashcardSession::Summary::description() const
{
    return QStringLiteral("%1 due · %2 new").arg(due).arg(newCount);
}

FlashcardSession::FlashcardSession(const CardDeck &deck, const QHash<QString, CardProgress> &progress, const QDateTime &now,
                                   bool shuffled, bool everything)
    : m_progress(progress)
{
    QSet<QString> seen;
    QList<Item> items;
    for (const Flashcard &card : deck.cards) {
        const QString k = key(card);
        if (seen.contains(k))
            continue;
        seen.insert(k);
        items.append({k, card});
    }
    QList<Item> due;
    QList<Item> fresh;
    for (const Item &item : items) {
        const auto found = progress.constFind(item.key);
        if (found != progress.constEnd() && FlashcardScheduler::isDue(*found, now))
            due.append(item);
        if (found == progress.constEnd())
            fresh.append(item);
    }
    std::stable_sort(due.begin(), due.end(), [&](const Item &a, const Item &b) { return progress[a.key].due < progress[b.key].due; });
    if (everything) {
        QSet<QString> queued;
        for (const Item &item : due)
            queued.insert(item.key);
        for (const Item &item : fresh)
            queued.insert(item.key);
        for (const Item &item : items)
            if (!queued.contains(item.key))
                due.append(item);
    }
    if (shuffled) {
        std::mt19937_64 random{std::random_device{}()};
        std::shuffle(due.begin(), due.end(), random);
        std::shuffle(fresh.begin(), fresh.end(), random);
    }
    m_queue = due + fresh;
}

void FlashcardSession::flip()
{
    if (m_queue.isEmpty())
        return;
    m_isFlipped = !m_isFlipped;
}

std::optional<FlashcardSession::Rated> FlashcardSession::rate(CardRating rating, const QDateTime &now)
{
    if (m_queue.isEmpty())
        return std::nullopt;
    const Item item = m_queue.first();
    std::optional<CardProgress> before;
    if (const auto found = m_progress.constFind(item.key); found != m_progress.constEnd())
        before = *found;
    const CardProgress next = FlashcardScheduler::next(before, rating, now);
    m_progress.insert(item.key, next);
    m_queue.removeFirst();
    if (rating == CardRating::again)
        m_queue.append(item);
    m_isFlipped = false;
    if (!m_answered.contains(item.key)) {
        m_answered.insert(item.key);
        m_stats.cards += 1;
    }
    m_stats.answers += 1;
    m_stats.ratings[rating] += 1;
    return Rated{item.key, next};
}

FlashcardSession::Summary FlashcardSession::summary(const CardDeck &deck, const QHash<QString, CardProgress> &progress,
                                                    const QDateTime &now)
{
    QSet<QString> keys;
    for (const Flashcard &card : deck.cards)
        keys.insert(key(card));
    Summary result;
    for (const QString &k : keys) {
        const auto found = progress.constFind(k);
        if (found == progress.constEnd())
            result.newCount += 1;
        else if (FlashcardScheduler::isDue(*found, now))
            result.due += 1;
    }
    return result;
}

QString FlashcardSession::key(const Flashcard &card)
{
    return StableHash::hex(card.question.simplified());
}

} // namespace wp
