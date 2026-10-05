#include "editor/BlockTextView.h"
#include "editor/DeckDraft.h"
#include "editor/EditorCommand.h"
#include "editor/MarkdownConcealment.h"
#include "editor/PageGeometry.h"

#include <QtTest>

using namespace wp;

namespace {

/// Applies `command` to `text` with the selection marked by `[` and `]`
/// (or a caret `|`), and returns the result marked the same way.
std::optional<QString> run(EditorCommand command, QString marked)
{
    TextRange selection;
    if (const int caret = int(marked.indexOf('|')); caret >= 0) {
        selection = TextRange(caret, 0);
        marked.remove(caret, 1);
    } else {
        const int open = int(marked.indexOf('['));
        marked.remove(open, 1);
        const int close = int(marked.lastIndexOf(']'));
        marked.remove(close, 1);
        selection = TextRange(open, close - open);
    }
    const auto change = MarkdownFormatting::apply(command, marked, selection);
    if (!change)
        return std::nullopt;
    QString result = change->applied(marked);
    const TextRange s = change->selection;
    if (s.length == 0) {
        result.insert(s.location, '|');
        return result;
    }
    const QString selected = result.mid(s.location, s.length);
    result.replace(s.location, s.length, "[" + selected + "]");
    return result;
}

#define EQ(actual, expected) QCOMPARE((actual).value_or(QStringLiteral("<nil>")), QString(expected))

} // namespace

class MarkdownFormattingTests : public QObject
{
    Q_OBJECT

private slots:
    void boldWrapsAndUnwraps()
    {
        EQ(run(EditorCommand::bold, "a [word] b"), "a **[word]** b");
        EQ(run(EditorCommand::bold, "a **[word]** b"), "a [word] b");
        EQ(run(EditorCommand::bold, "a [**word**] b"), "a [word] b");
        EQ(run(EditorCommand::bold, "a [word ]b"), "a **[word]** b"); // trailing space stays outside
    }

    void emptySelectionInsertsAndRemovesAPair()
    {
        EQ(run(EditorCommand::bold, "a |"), "a **|**");
        EQ(run(EditorCommand::bold, "a **|**"), "a |");
        EQ(run(EditorCommand::inlineCode, "x|"), "x`|`");
    }

    void italicIsNotConfusedWithBold()
    {
        EQ(run(EditorCommand::italic, "[x]"), "_[x]_");
        EQ(run(EditorCommand::italic, "_[x]_"), "[x]");
        EQ(run(EditorCommand::italic, "__[x]__"), "___[x]___");
    }

    void wrapEachLineOfAMultilineSelection()
    {
        EQ(run(EditorCommand::inlineCode, "[a\n\nb]"), "[`a`\n\n`b`]");
        EQ(run(EditorCommand::inlineCode, "[`a`\n`b`]"), "[a\nb]");
    }

    void noInlineFormattingInsideCode() { QVERIFY(!run(EditorCommand::bold, "```\n[x]\n```")); }

    void headingsReplaceAndToggle()
    {
        EQ(run(EditorCommand::heading1, "Ti|tle"), "# Ti|tle");
        EQ(run(EditorCommand::heading2, "# Ti|tle"), "## Ti|tle");
        EQ(run(EditorCommand::heading2, "## Ti|tle"), "Ti|tle");
        EQ(run(EditorCommand::heading1, "- [ ] ta|sk"), "# ta|sk");
        EQ(run(EditorCommand::body, "### Ti|tle"), "Ti|tle");
        QVERIFY(!run(EditorCommand::body, "Ti|tle"));
    }

    void listsApplyToEverySelectedLine()
    {
        EQ(run(EditorCommand::bulletList, "[a\nb]"), "[- a\n- b]");
        EQ(run(EditorCommand::bulletList, "[- a\n- b]"), "[a\nb]");
        EQ(run(EditorCommand::numberedList, "[a\n\nb\nc]"), "[1. a\n\n2. b\n3. c]");
        EQ(run(EditorCommand::checklist, "    - it|em"), "    - [ ] it|em"); // list indent is kept
        EQ(run(EditorCommand::quote, "|said"), "> |said");
    }

    void codeBlockFencesTheLines()
    {
        EQ(run(EditorCommand::codeBlock, "|"), "```\n|\n```");
        EQ(run(EditorCommand::codeBlock, "x\n[let a\nlet b]"), "x\n```\n[let a\nlet b]\n```");
    }

    void dividerAfterTheLine()
    {
        EQ(run(EditorCommand::divider, "text|"), "text\n\n---\n|");
        EQ(run(EditorCommand::divider, "|"), "---\n|");
    }

    void insertCommandsAreNotTextEdits()
    {
        QVERIFY(!run(EditorCommand::drawing, "|"));
        QVERIFY(!run(EditorCommand::flashcards, "|"));
        QVERIFY(!run(EditorCommand::newPage, "|"));
    }

    void commandMetadata()
    {
        QCOMPARE(EditorCommands::all().size(), 16);
        QCOMPARE(EditorCommands::title(EditorCommand::heading1), QString("Heading 1"));
        QVERIFY(EditorCommands::fromRawValue("inlineCode") == EditorCommand::inlineCode);
        QVERIFY(!EditorCommands::fromRawValue("nope"));
    }

    // MARK: Decks

    void deckDraft()
    {
        DeckDraft draft(CardDeck("c1", QString("T"), {Flashcard("Q1", "A1")}));
        QCOMPARE(draft.addCard(), 1);
        draft.cards[1].question = "  Q2 ";
        draft.cards[1].ref = " ";
        draft.addCard();
        QVERIFY(draft.moveCard(1, -1));
        QVERIFY(!draft.moveCard(0, -1));
        draft.title = "  ";
        QVERIFY(draft.deck() == CardDeck(QString(), std::nullopt, {Flashcard("Q2", ""), Flashcard("Q1", "A1")}));
        draft.removeCard(0);
        QCOMPARE(draft.deck().cards.size(), 1);
        QCOMPARE(draft.deck().cards[0].question, QString("Q1"));
    }

    // MARK: Geometry

    void a4FitsTheWidthWithPrintMargins()
    {
        const PageGeometry wide(2000, PageLayoutMode::a4, PageGeometry::a4Paper());
        QVERIFY(std::abs(wide.fontSize - PageGeometry::paperFontSize * PageGeometry::maxPaperScale) < 0.1);
        QVERIFY(std::abs(wide.pageWidth - 595.28 * 1.3) < 4);
        QVERIFY(std::abs(wide.padding - 56 * 1.3) < 1);
        const PageGeometry narrow(500, PageLayoutMode::a4, PageGeometry::a4Paper());
        QVERIFY(narrow.pageWidth <= 500 - 2 * PageGeometry::deskMargin);
        QVERIFY(std::abs(narrow.textWidth() / narrow.fontSize - (595.28 - 112) / 11) < 1); // text wraps as on the printed page
    }

    void paperByRegion()
    {
        QCOMPARE(PageGeometry::paperSize(QString("US")), PageGeometry::letterPaper());
        QCOMPARE(PageGeometry::paperSize(QString("CA")), PageGeometry::letterPaper());
        QCOMPARE(PageGeometry::paperSize(QString("BE")), PageGeometry::a4Paper());
        QCOMPARE(PageGeometry::paperSize(std::nullopt), PageGeometry::a4Paper());
    }

    void a4SheetsGrowByPrintedPages()
    {
        const PageGeometry geometry(1000, PageLayoutMode::a4, PageGeometry::a4Paper());
        QVERIFY(geometry.printableHeight);
        const double printable = *geometry.printableHeight;
        QCOMPARE(geometry.sheet(10).height, geometry.minPageHeight());
        QVERIFY(geometry.sheet(10).breaks.empty());
        const auto two = geometry.sheet(printable + 1);
        QCOMPARE(two.breaks.size(), size_t(1));
        QCOMPARE(two.breaks[0], geometry.topPadding + printable);
        QVERIFY(std::abs(two.height - (2 * geometry.topPadding + 2 * printable)) < 1.5);
        QCOMPARE(geometry.sheet(3 * printable).breaks.size(), size_t(2));
    }

    void slidesAreUnchanged()
    {
        const PageGeometry geometry(1000);
        QCOMPARE(geometry.textWidth(), PageGeometry::maxTextWidth);
        QCOMPARE(geometry.fontSize, 15.0);
        QCOMPARE(geometry.sheet(1000).height, 60.0 + 1000 + 72);
        QVERIFY(geometry.sheet(1000).breaks.empty());
    }

    // MARK: Concealed glyphs

    void concealedGlyphMapping()
    {
        using M = MarkdownStyler::Markup;
        QVERIFY(ConcealedGlyphs::isZeroWidth(M::hidden));
        QVERIFY(ConcealedGlyphs::isZeroWidth(M::rule));
        QVERIFY(!ConcealedGlyphs::isZeroWidth(M::bullet));
        QVERIFY(!ConcealedGlyphs::isZeroWidth(M::checkbox));
        QVERIFY(!ConcealedGlyphs::isZeroWidth(M::checkedBox));
        QCOMPARE(ConcealedGlyphs::bulletCharacter(), QChar(0x2022));
    }

    void revealedRangeFollowsEdits()
    {
        const QString text = "aa\nbb\ncc";
        const TextRange revealed(3, 3);
        QVERIFY(BlockTextView::range(revealed, TextRange(0, 2), 2, "xxaa\nbb\ncc") == TextRange(5, 3)); // edit before moves it
        QVERIFY(BlockTextView::range(revealed, TextRange(7, 1), 1, text) == revealed); // edit after leaves it
        QVERIFY(BlockTextView::range(revealed, TextRange(4, 1), 1, "aa\nbxb\ncc") == TextRange(3, 4)); // edit inside grows it
    }
};

QTEST_MAIN(MarkdownFormattingTests)
#include "MarkdownFormattingTests.moc"
