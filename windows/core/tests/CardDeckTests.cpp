#include "TestSupport.h"
#include "core/Note.h"

using namespace wp;
using namespace wp::testing;

class CardDeckTests : public QObject
{
    Q_OBJECT

private:
    static QStringList blockIDs(const Note &note, int page)
    {
        QStringList ids;
        for (const auto &b : note.pages()[page].blocks) {
            if (auto id = b.id())
                ids.append(*id);
        }
        return ids;
    }

private slots:
    void parsesTitleCardsRefsAndMultiLineFields()
    {
        const CardDeck deck = CardDeck::parsing("c1", U(
            "# TCP basics\n"
            "Q: What does TCP guarantee?\n"
            "A: Ordered, reliable delivery.\n"
            "It retransmits lost segments.\n"
            "ref: Lecture3.pptx \xc2\xb7 slide 4\n"
            "\n"
            "Q: Handshake?\n"
            "A: SYN, SYN-ACK, ACK"));
        WP_EQ(deck.title, std::optional<QString>(QString("TCP basics")));
        const QList<Flashcard> expected = {
            Flashcard("What does TCP guarantee?", "Ordered, reliable delivery.\nIt retransmits lost segments.",
                      U("Lecture3.pptx \xc2\xb7 slide 4")),
            Flashcard("Handshake?", "SYN, SYN-ACK, ACK"),
        };
        WP_EQ(deck.cards, expected);
    }

    void sourceRoundTripsAndEscapesStructuralLines()
    {
        const CardDeck deck(QString("c1"), QString("T"),
                            {Flashcard("Two\nlines", "A: not a field\n\nQ: nor this\n```\n\\x", QString("p. 2")),
                             Flashcard("Q", "")});
        const CardDeck parsed = CardDeck::parsing("c1", deck.source());
        WP_EQ(parsed.cards[0],
              Flashcard("Two\nlines", "A: not a field\nQ: nor this\n```\n\\x", QString("p. 2")));
        WP_EQ(parsed.cards[1], Flashcard("Q", ""));
        WP_EQ(parsed.title, std::optional<QString>(QString("T")));
    }

    void decksInNotesGetIDsAndRoundTrip()
    {
        const Note note = Note::parsing("Intro\n\n```cards\nQ: a\nA: b\n```\n\n```wp\nbox a\n```");
        QCOMPARE(note.decks().size(), qsizetype(1));
        QCOMPARE(note.decks()[0].id, QString("c1"));
        QCOMPARE(note.drawings().size(), qsizetype(1));
        QCOMPARE(note.drawings()[0].id, QString("d1"));
        QCOMPARE(blockIDs(note, 0), (QStringList{"c1", "d1"}));
        WP_EQ(Note::parsing(note.serialized()), note);
        QVERIFY(note.serialized().contains(QLatin1String("```cards id=c1\nQ: a\nA: b\n```")));
    }

    void deckEditingNeverReusesIDs()
    {
        Note note = Note::parsing("x");
        const QString first = note.insertDeck(CardDeck({Flashcard("q", "a")}), 1);
        const QString drawing = note.insertDrawing("box a", 1);
        const QString second = note.insertDeck(CardDeck(QString(), QString("Two"), {}), 1, first);
        QCOMPARE((QStringList{first, drawing, second}), (QStringList{"c1", "d1", "c2"}));
        QCOMPARE(blockIDs(note, 0), (QStringList{"c1", "c2", "d1"}));

        note.updateDeck("c2", CardDeck(QString("ignored"), QString("Renamed"), {}));
        QCOMPARE(note.deck("c2")->deck.title.value(), QString("Renamed"));
        QCOMPARE(note.deck("c2")->deck.id, QString("c2"));
        note.deleteDeck("c2");
        QVERIFY(!note.deck("c2").has_value());
        QCOMPARE(Note::parsing(note.serialized()).nextDeckID(), QString("c3"));
        const auto error = caught<NoteEditError>([&] { note.deleteDeck("c2"); });
        QVERIFY(error.has_value());
        QCOMPARE(error->description(), QString("no flashcard deck 'c2' in this note"));
    }

    void replaceKeepsDeckIDsWrittenBack()
    {
        Note note = Note::parsing("```cards id=c1\nQ: a\nA: b\n```");
        note.write("```cards id=c1\nQ: a2\nA: b2\n```", 1, WriteMode::replace);
        WP_EQ(note.decks(), (QList<CardDeck>{CardDeck("c1", std::nullopt, {Flashcard("a2", "b2")})}));
    }

    void markdownExportListsCards()
    {
        const Note note = Note::parsing("```cards\n# Deck\nQ: What?\nA: That.\nref: p. 1\n```");
        QCOMPARE(note.markdown(), QString("**Flashcards: Deck**\n\n- **What?**\n  That. *(p. 1)*\n"));
    }

    void typedStructureInTextIsEscaped()
    {
        const Note note(QList<NotePage>{
            NotePage({NoteBlock::textBlock("a\n+++page\n```wp\nbox\n```\n```swift\n+++page\n```")})});
        const Note reparsed = Note::parsing(note.serialized());
        QCOMPARE(reparsed.pages().size(), qsizetype(1));
        QVERIFY(reparsed.drawings().isEmpty());
        WP_EQ(reparsed.pages()[0].blocks,
              (QList<NoteBlock>{NoteBlock::textBlock("a\n\\+++page\n\\```wp\nbox\n```\n```swift\n+++page\n```")}));
    }
};

QTEST_APPLESS_MAIN(CardDeckTests)
#include "CardDeckTests.moc"
