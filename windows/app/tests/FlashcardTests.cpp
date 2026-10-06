// Port of FlashcardTests.swift.
#include "app/DeckCatalog.h"
#include "app/FlashcardProgressStore.h"
#include "app/FlashcardScheduler.h"
#include "app/FlashcardSession.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

using namespace wp;

namespace {
const QDateTime now = QDateTime::fromSecsSinceEpoch(1'800'000'000, QTimeZone::UTC);
constexpr double day = 86'400;

QDateTime plus(double seconds) { return now.addMSecs(qint64(seconds * 1000)); }

CardDeck makeDeck()
{
    return CardDeck("c1", QString("TCP"),
                    {Flashcard("Q1", "A1"), Flashcard("Q2", "A2"), Flashcard("Q3", "A3"), Flashcard("Q1", "duplicate")});
}

QString key(const QString &question) { return FlashcardSession::key(Flashcard(question, "")); }

QStringList questions(const QList<FlashcardSession::Item> &queue)
{
    QStringList result;
    for (const auto &item : queue)
        result << item.card.question;
    return result;
}
} // namespace

class FlashcardTests : public QObject {
    Q_OBJECT

private slots:
    // Scheduler
    void newCardIntervals()
    {
        QCOMPARE(FlashcardScheduler::next(std::nullopt, CardRating::again, now).due, plus(600));
        QCOMPARE(FlashcardScheduler::next(std::nullopt, CardRating::hard, now).interval, 1.0);
        QCOMPARE(FlashcardScheduler::next(std::nullopt, CardRating::good, now).interval, 1.0);
        const auto easy = FlashcardScheduler::next(std::nullopt, CardRating::easy, now);
        QCOMPARE(easy.interval, 4.0);
        QCOMPARE(easy.due, plus(4 * day));
        QVERIFY(qAbs(easy.ease - 2.65) < 0.001);
    }

    void goodAnswersGrowTheInterval()
    {
        auto card = FlashcardScheduler::next(std::nullopt, CardRating::good, now);
        card = FlashcardScheduler::next(card, CardRating::good, now);
        QCOMPARE(card.interval, 3.0);
        card = FlashcardScheduler::next(card, CardRating::good, now);
        QVERIFY(qAbs(card.interval - 7.5) < 0.01);
        QCOMPARE(card.reps, 3);
        QVERIFY(qAbs(card.ease - 2.5) < 0.001);
    }

    void lapseResetsAndLowersEase()
    {
        auto card = FlashcardScheduler::next(std::nullopt, CardRating::good, now);
        card = FlashcardScheduler::next(card, CardRating::good, now);
        card = FlashcardScheduler::next(card, CardRating::again, now);
        QCOMPARE(card.reps, 0);
        QCOMPARE(card.lapses, 1);
        QCOMPARE(card.interval, 0.0);
        QVERIFY(qAbs(card.ease - 2.3) < 0.001);
        for (int i = 0; i < 20; ++i)
            card = FlashcardScheduler::next(card, CardRating::again, now);
        QVERIFY(qAbs(card.ease - FlashcardScheduler::minimumEase) < 0.001);
    }

    void dueAndLabels()
    {
        const auto card = FlashcardScheduler::next(std::nullopt, CardRating::good, now);
        QVERIFY(!FlashcardScheduler::isDue(card, now));
        QVERIFY(FlashcardScheduler::isDue(card, plus(day)));
        QVERIFY(!FlashcardScheduler::isDue(std::nullopt, now));
        QCOMPARE(FlashcardScheduler::intervalLabel(std::nullopt, CardRating::again, now), QStringLiteral("10m"));
        QCOMPARE(FlashcardScheduler::intervalLabel(std::nullopt, CardRating::good, now), QStringLiteral("1d"));
        QCOMPARE(FlashcardScheduler::intervalLabel(std::nullopt, CardRating::easy, now), QStringLiteral("4d"));
    }

    // Session
    void queuesDueBeforeNewAndSkipsLaterCards()
    {
        const CardDeck deck = makeDeck();
        QHash<QString, CardProgress> progress;
        progress[key("Q2")] = CardProgress{1, 2.5, 1, 0, plus(-60)};
        progress[key("Q3")] = CardProgress{5, 2.5, 2, 0, plus(3600)};
        const FlashcardSession session(deck, progress, now);
        QCOMPARE(questions(session.queue()), (QStringList{"Q2", "Q1"}));
        const auto summary = FlashcardSession::summary(deck, progress, now);
        QCOMPARE(summary.due, 1);
        QCOMPARE(summary.newCount, 1);
        QCOMPARE(summary.description(), QStringLiteral("1 due · 1 new"));
        const FlashcardSession everything(deck, progress, now, false, true);
        QCOMPARE(questions(everything.queue()), (QStringList{"Q2", "Q3", "Q1"}));
    }

    void flipRateAndRequeueOnAgain()
    {
        FlashcardSession session(makeDeck(), {}, now);
        QCOMPARE(session.current()->card.question, QStringLiteral("Q1"));
        session.flip();
        QVERIFY(session.isFlipped());
        const auto saved = session.rate(CardRating::again, now);
        QVERIFY(saved);
        QCOMPARE(saved->key, key("Q1"));
        QVERIFY(!session.isFlipped());
        QCOMPARE(questions(session.queue()), (QStringList{"Q2", "Q3", "Q1"}));
        session.rate(CardRating::good, now);
        session.rate(CardRating::good, now);
        session.rate(CardRating::easy, now);
        QVERIFY(session.isFinished());
        QCOMPARE(session.stats().cards, 3);
        QCOMPARE(session.stats().answers, 4);
        const std::map<CardRating, int> expected{{CardRating::good, 2}, {CardRating::again, 1}, {CardRating::easy, 1}};
        QVERIFY(session.stats().ratings == expected);
        QCOMPARE(session.progress().size(), 3);
    }

    void shuffleKeepsTheCards()
    {
        const FlashcardSession session(makeDeck(), {}, now, true);
        QStringList q = questions(session.queue());
        q.sort();
        QCOMPARE(q, (QStringList{"Q1", "Q2", "Q3"}));
    }

    void keysAreStableAndIgnoreSpacing()
    {
        QCOMPARE(StableHash::hex(""), QStringLiteral("cbf29ce484222325"));
        QCOMPARE(StableHash::hex("a"), QStringLiteral("af63dc4c8601ec8c"));
        QCOMPARE(key("What  is\nTCP?"), key("What is TCP?"));
        QVERIFY(key("Q1") != key("Q2"));
    }

    void deckCatalog()
    {
        NoteEntry note;
        note.url = "/notes/TCP.wprint";
        note.title = "Networks";
        note.pageCount = 1;
        note.folder = QString();
        note.decks = {makeDeck(), CardDeck("c2", std::nullopt, {}), CardDeck("c3", QString(" "), {Flashcard("Q", "A")})};
        const auto entries = DeckCatalog::entries({note}, [](const QString &, const QString &) { return QHash<QString, CardProgress>(); }, now);
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0].deck.id, QStringLiteral("c1"));
        QCOMPARE(entries[1].deck.id, QStringLiteral("c3"));
        QCOMPARE(entries[0].title(), QStringLiteral("TCP"));
        QCOMPARE(entries[1].title(), QStringLiteral("Networks"));
        QCOMPARE(entries[0].summary.due, 0);
        QCOMPARE(entries[0].summary.newCount, 3);
    }

    // Progress store
    void savesPerNoteDeckAndCard()
    {
        QTemporaryDir dir;
        const QString note = "/notes/TCP.wprint";
        const CardProgress progress{3, 2.5, 2, 0, now};
        FlashcardProgressStore store(dir.path());
        store.save(progress, "k1", note, "c1");
        FlashcardProgressStore reopened(dir.path());
        const auto loaded = reopened.progress(note, "c1");
        QCOMPARE(loaded.size(), 1);
        QVERIFY(loaded.value("k1") == progress);
        QVERIFY(store.progress(note, "c2").isEmpty());
        QVERIFY(store.progress("/notes/Other.wprint", "c1").isEmpty());
    }

    void progressFollowsMovedNotesAndFolders()
    {
        QTemporaryDir dir;
        const QString note = "/notes/Courses/TCP.wprint";
        const CardProgress progress{1, 2.5, 1, 0, now};
        FlashcardProgressStore store(dir.path());
        store.save(progress, "k1", note, "c1");
        store.moveNotes("/notes/Courses", "/notes/Archive");
        QVERIFY(store.progress(note, "c1").isEmpty());
        const QString moved = "/notes/Archive/TCP.wprint";
        FlashcardProgressStore reopened(dir.path());
        QVERIFY(reopened.progress(moved, "c1").value("k1") == progress);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);
    }
};

QTEST_GUILESS_MAIN(FlashcardTests)
#include "FlashcardTests.moc"
