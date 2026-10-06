#include "Bitmap.h"
#include "render/Fonts.h"
#include "render/MarkdownStyler.h"

#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QtTest>
#include <cmath>

using namespace wp;
namespace S = wp::MarkdownStyler;

namespace {

const BlueprintPalette palette = BlueprintPalette::blueprint();
constexpr int testMarkerProperty = QTextFormat::UserProperty + 0x900;

using Doc = std::unique_ptr<QTextDocument>;

Doc styled(const QString &markdown)
{
    return S::attributedString(markdown, palette);
}

QFont fontAt(const QTextDocument &d, int i) { return S::formatAt(d, i).font(); }
double sizeAt(const QTextDocument &d, int i) { return fonts::sizeOf(fontAt(d, i)); }
QColor colorAt(const QTextDocument &d, int i) { return S::formatAt(d, i).foreground().color(); }
QTextBlockFormat paragraphAt(const QTextDocument &d, int i) { return d.findBlock(i).blockFormat(); }
bool isBold(const QFont &f) { return f.weight() >= QFont::Bold; }
bool isItalic(const QFont &f) { return f.italic(); }
bool isMono(const QFont &f) { return fonts::isMonospaced(f); }
int length(const QTextDocument &d) { return d.characterCount() - 1; }

void replaceText(QTextDocument &d, TextRange range, const QString &replacement)
{
    QTextCursor c(&d);
    c.setPosition(range.location);
    c.setPosition(range.end(), QTextCursor::KeepAnchor);
    if (replacement.isEmpty())
        c.removeSelectedText();
    else
        c.insertText(replacement);
}

/// Whether two documents have the same text and the same formats everywhere.
bool sameStyling(const QTextDocument &a, const QTextDocument &b, QString *why = nullptr)
{
    auto fail = [&](const QString &m) {
        if (why)
            *why = m;
        return false;
    };
    if (a.toPlainText() != b.toPlainText())
        return fail(QStringLiteral("text"));
    if (a.blockCount() != b.blockCount())
        return fail(QStringLiteral("block count"));
    QTextBlock x = a.firstBlock(), y = b.firstBlock();
    for (; x.isValid() && y.isValid(); x = x.next(), y = y.next()) {
        if (x.blockFormat() != y.blockFormat())
            return fail(QStringLiteral("block format of %1").arg(x.text()));
        if (x.charFormat() != y.charFormat())
            return fail(QStringLiteral("block char format of %1").arg(x.text()));
        for (int i = 0; i < x.length() - 1; ++i)
            if (S::formatAt(a, x.position() + i) != S::formatAt(b, y.position() + i))
                return fail(QStringLiteral("char %1 of %2").arg(i).arg(x.text()));
    }
    return true;
}

const QString document = QStringLiteral("# Notes\nSome **bold** text\n\n- [ ] task\n\n```\ncode **x**\n```\n\nafter *it*\n> quote");

} // namespace

class MarkdownStylerTests : public QObject
{
    Q_OBJECT

private:
    /// Applies `edit` to a fully styled document, restyles only the edited
    /// range and compares with styling the result from scratch.
    static bool incrementalMatchesFull(const QString &initial, TextRange range, const QString &replacement)
    {
        QTextDocument doc;
        doc.setPlainText(initial);
        S::apply(&doc, std::nullopt, palette);
        replaceText(doc, range, replacement);
        S::apply(&doc, TextRange(range.location, int(replacement.size())), palette);
        QTextDocument full;
        full.setPlainText(doc.toPlainText());
        S::apply(&full, std::nullopt, palette);
        QString why;
        const bool ok = sameStyling(doc, full, &why);
        if (!ok)
            qWarning("incremental restyle differs (%s) for %s", qPrintable(why), qPrintable(doc.toPlainText()));
        return ok;
    }

    /// Each run of the markup property as `text=markup`, in order.
    static QStringList markup(const QTextDocument &d)
    {
        QStringList runs;
        for (QTextBlock b = d.firstBlock(); b.isValid(); b = b.next()) {
            const QString text = b.text();
            int i = 0;
            while (i < text.size()) {
                const auto m = S::markupOf(S::formatAt(d, b.position() + i));
                int j = i + 1;
                while (j < text.size() && S::markupOf(S::formatAt(d, b.position() + j)) == m)
                    ++j;
                if (m)
                    runs.append(text.mid(i, j - i) + QLatin1Char('=') + S::markupName(*m));
                i = j;
            }
        }
        return runs;
    }

    static Doc concealed(const QString &markdown, std::optional<TextRange> revealing = std::nullopt)
    {
        auto doc = std::make_unique<QTextDocument>();
        doc->setPlainText(markdown);
        S::apply(doc.get(), std::nullopt, palette, S::defaultFontSize, true, revealing);
        return doc;
    }

    static int at(const QString &text, const QString &needle) { return int(text.indexOf(needle)); }

private slots:
    // MARK: Blocks

    void textStaysRawMarkdown()
    {
        const QString markdown = QStringLiteral("# Title\n- [x] **done**\n```\ncode\n```");
        QCOMPARE(styled(markdown)->toPlainText(), markdown);
    }

    void bodyText()
    {
        const auto text = styled("Hello");
        QCOMPARE(sizeAt(*text, 0), S::defaultFontSize);
        QCOMPARE(colorAt(*text, 0), palette.text);
        QVERIFY(S::lineSpacing(paragraphAt(*text, 0)) > 0);
    }

    void headingSizesAndMutedMarkers()
    {
        const auto text = styled("# One\n## Two\n### Three\n#### Four");
        QCOMPARE(sizeAt(*text, 2), 28.0);
        QCOMPARE(sizeAt(*text, 9), 22.0);
        QCOMPARE(sizeAt(*text, 17), 18.0);
        QCOMPARE(sizeAt(*text, 29), 15.0);
        QCOMPARE(colorAt(*text, 0), palette.muted);
        QCOMPARE(colorAt(*text, 2), palette.text);
    }

    void headingsScaleWithFontSize()
    {
        const auto text = S::attributedString("# Big", palette, 30);
        QCOMPARE(sizeAt(*text, 2), 56.0);
    }

    void listsHaveAHangingIndent()
    {
        const auto text = styled("- item\n10. ten");
        const QTextBlockFormat bullet = paragraphAt(*text, 2);
        QCOMPARE(S::firstLineHeadIndent(bullet), 0.0);
        QVERIFY(S::headIndent(bullet) > 0);
        QVERIFY(S::headIndent(paragraphAt(*text, 10)) > S::headIndent(bullet));
        QCOMPARE(colorAt(*text, 0), palette.muted);
        QCOMPARE(colorAt(*text, 2), palette.text);
    }

    void checklists()
    {
        const auto text = styled("- [ ] open\n- [x] done");
        QVERIFY(!fontAt(*text, 7).strikeOut());
        QCOMPARE(colorAt(*text, 7), palette.text);
        QVERIFY(fontAt(*text, 17).strikeOut());
        QCOMPARE(colorAt(*text, 17), palette.muted);
        QCOMPARE(colorAt(*text, 13), palette.accent); // checked box
    }

    void quotesAreMutedAndIndented()
    {
        const auto text = styled("> quoted");
        QCOMPARE(colorAt(*text, 4), palette.muted);
        QVERIFY(S::firstLineHeadIndent(paragraphAt(*text, 4)) > 0);
    }

    void dividerIsMuted()
    {
        QCOMPARE(colorAt(*styled("---"), 1), palette.muted);
    }

    void codeFencesAreMonospacedAndUnstyledInside()
    {
        const QString markdown = QStringLiteral("```\n# **x**\n```\nafter **y**");
        const auto text = styled(markdown);
        const int inside = at(markdown, "**x**");
        QVERIFY(isMono(fontAt(*text, inside + 2)));
        QVERIFY(!isBold(fontAt(*text, inside + 2)));
        QCOMPARE(colorAt(*text, inside), palette.text); // markup inside code is not muted
        QCOMPARE(colorAt(*text, 0), palette.muted);     // fence line
        QVERIFY(S::formatAt(*text, inside).background().style() != Qt::NoBrush);
        QVERIFY(S::formatAt(*text, 0).boolProperty(S::codeBlockProperty));

        const int after = at(markdown, "y");
        QVERIFY(isBold(fontAt(*text, after)));
        QVERIFY(!S::formatAt(*text, after).hasProperty(S::codeBlockProperty));
    }

    void unclosedFenceRunsToTheEnd()
    {
        const auto text = styled("text\n```\n**a**\n\n# b");
        QVERIFY(isMono(fontAt(*text, length(*text) - 1)));
    }

    // MARK: Inline

    void inlineStyles()
    {
        const QString markdown = QStringLiteral("a **b** *c* `d` [e](f)");
        const auto text = styled(markdown);
        QVERIFY(isBold(fontAt(*text, at(markdown, "b"))));
        QCOMPARE(colorAt(*text, at(markdown, "**")), palette.muted);
        QVERIFY(isItalic(fontAt(*text, at(markdown, "c"))));
        QVERIFY(isMono(fontAt(*text, at(markdown, "d"))));
        QVERIFY(S::formatAt(*text, at(markdown, "d")).background().style() != Qt::NoBrush);
        QCOMPARE(colorAt(*text, at(markdown, "e")), palette.accent);
        QCOMPARE(S::formatAt(*text, at(markdown, "e")).stringProperty(S::linkProperty), QStringLiteral("f"));
        QCOMPARE(colorAt(*text, at(markdown, "(f")), palette.muted);
        QVERIFY(!S::formatAt(*text, at(markdown, "e")).isAnchor());
    }

    void boldInsideHeadingKeepsHeadingSize()
    {
        const auto text = styled("# a **b**");
        QCOMPARE(sizeAt(*text, 6), 28.0);
        QVERIFY(isBold(fontAt(*text, 6)));
    }

    void boldItalic()
    {
        const auto text = styled("***x***");
        QVERIFY(isBold(fontAt(*text, 3)));
        QVERIFY(isItalic(fontAt(*text, 3)));
    }

    // MARK: Incremental restyling

    void incrementalTypingInAParagraph()
    {
        const int bold = at(document, "bold");
        QVERIFY(incrementalMatchesFull(document, TextRange(bold, 0), "very "));
        QVERIFY(incrementalMatchesFull(document, TextRange(bold - 2, 2), ""));
    }

    void incrementalChangeOfBlockKind()
    {
        const int some = at(document, "Some");
        QVERIFY(incrementalMatchesFull(document, TextRange(some, 0), "## "));
        QVERIFY(incrementalMatchesFull(document, TextRange(0, 2), ""));
        const int task = at(document, "[ ]");
        QVERIFY(incrementalMatchesFull(document, TextRange(task + 1, 1), "x"));
    }

    void incrementalOpeningAFenceRestylesTheLinesBelow()
    {
        QVERIFY(incrementalMatchesFull(document, TextRange(at(document, "Some"), 0), "```\n"));
    }

    void incrementalRemovingAFenceRestylesTheLinesBelow()
    {
        QVERIFY(incrementalMatchesFull(document, TextRange(at(document, "```\ncode"), 8), "code"));
        const int closing = at(document, "```\n\nafter");
        QVERIFY(incrementalMatchesFull(document, TextRange(closing, 1), ""));
    }

    void incrementalEditsInsideAndAroundCode()
    {
        const int code = at(document, "code");
        QVERIFY(incrementalMatchesFull(document, TextRange(code, 0), "# "));
        QVERIFY(incrementalMatchesFull(document, TextRange(code, 0), "line\n"));
    }

    void incrementalAtTheEdges()
    {
        const int n = int(document.size());
        QVERIFY(incrementalMatchesFull(document, TextRange(n, 0), "\n"));
        QVERIFY(incrementalMatchesFull(document, TextRange(n, 0), "\n- more"));
        QVERIFY(incrementalMatchesFull(document, TextRange(0, 0), "```\n"));
        QVERIFY(incrementalMatchesFull(document, TextRange(0, n), ""));
        QVERIFY(incrementalMatchesFull(document, TextRange(0, n), "x"));
    }

    void incrementalRestyleStaysLocal()
    {
        QStringList lines;
        for (int i = 0; i < 200; ++i)
            lines.append(QStringLiteral("line %1 with **bold**").arg(i));
        QTextDocument doc;
        doc.setPlainText(lines.join(QLatin1Char('\n')));
        S::apply(&doc, std::nullopt, palette);
        // Paint the last line red: only a restyle of that line could undo it.
        const QTextBlock last = doc.lastBlock();
        QTextCursor cursor(&doc);
        cursor.setPosition(last.position());
        cursor.setPosition(last.position() + int(last.text().size()), QTextCursor::KeepAnchor);
        QTextCharFormat red;
        red.setForeground(QBrush(Qt::red));
        red.setProperty(testMarkerProperty, true);
        cursor.mergeCharFormat(red);

        replaceText(doc, TextRange(3, 0), "x");
        S::apply(&doc, TextRange(3, 1), palette);
        const int lastIndex = length(doc) - 1;
        QCOMPARE(colorAt(doc, lastIndex), QColor(Qt::red));
        QVERIFY(S::formatAt(doc, lastIndex).boolProperty(testMarkerProperty));
        QVERIFY(isBold(fontAt(doc, lastIndex - 3)));
        // ... while the edited line was restyled.
        QCOMPARE(colorAt(doc, 3), palette.text);
    }

    void restylingKeepsOtherProperties()
    {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral("a ￼ b"));
        QTextCursor cursor(&doc);
        cursor.setPosition(2);
        cursor.setPosition(3, QTextCursor::KeepAnchor);
        QTextCharFormat attachment;
        attachment.setProperty(testMarkerProperty, 42);
        cursor.mergeCharFormat(attachment);
        S::apply(&doc, std::nullopt, palette);
        QCOMPARE(S::formatAt(doc, 2).intProperty(testMarkerProperty), 42);
        QCOMPARE(S::formatAt(doc, 2).foreground().color(), palette.text);
    }

    void outOfRangeRequestIsClamped()
    {
        QTextDocument doc;
        doc.setPlainText("# hi");
        S::apply(&doc, TextRange(99, 5), palette);
        QCOMPARE(sizeAt(doc, 3), 28.0);
        QTextDocument empty;
        S::apply(&empty, TextRange(0, 0), palette);
    }

    // MARK: Concealing markup

    void shownMarkupIsNotMarked()
    {
        QCOMPARE(markup(*styled("# a **b** `c`\n- d\n---")), QStringList());
    }

    void concealedBlockMarkers()
    {
        const auto text = concealed("# Title\n- item\n- [ ] open\n- [x] done\n> quote\n1. one\n---");
        QCOMPARE(markup(*text), (QStringList{"# =hidden", "-=bullet", "- =hidden", "[ ]=checkbox", "- =hidden", "[x]=checkedBox",
                                              "> =hidden", "---=rule"}));
        const int box = at(text->toPlainText(), "[x]");
        QCOMPARE(colorAt(*text, box), QColor(Qt::transparent)); // the box is drawn by the editor
    }

    void concealedInlineMarkup()
    {
        const QString markdown = QStringLiteral("a **b** _c_ `d` [e](f)");
        QCOMPARE(markup(*concealed(markdown)), (QStringList{"**=hidden", "**=hidden", "_=hidden", "_=hidden", "`=hidden", "`=hidden",
                                                             "[=hidden", "](f)=hidden"}));
        QVERIFY(isBold(fontAt(*concealed(markdown), 4)));
    }

    void concealedFenceLinesButNotCode()
    {
        QCOMPARE(markup(*concealed("```swift\nlet **x**\n```")), (QStringList{"```swift=hidden", "```=hidden"}));
    }

    void theRevealedLineKeepsItsMarkup()
    {
        const QString markdown = QStringLiteral("# One\n**two**\n# Three");
        QCOMPARE(markup(*concealed(markdown, TextRange(8, 0))), (QStringList{"# =hidden", "# =hidden"}));
        QCOMPARE(markup(*concealed(markdown, TextRange(3, 8))), (QStringList{"# =hidden"}));
        // caret at the start of a line reveals that line
        QCOMPARE(markup(*concealed(markdown, TextRange(6, 0))), (QStringList{"# =hidden", "# =hidden"}));
        const int n = int(markdown.size());
        QCOMPARE(markup(*concealed(markdown, TextRange(n, 0))), (QStringList{"# =hidden", "**=hidden", "**=hidden"}));
        // caret on the empty last line
        QCOMPARE(markup(*concealed("a\n# b\n", TextRange(6, 0))), (QStringList{"# =hidden"}));
    }

    void concealedListsIndentByTheShownMarker()
    {
        const auto task = concealed("- [ ] task");
        const double width = QFontMetricsF(fonts::system(15)).horizontalAdvance(QStringLiteral("[ ] "));
        QVERIFY(std::abs(S::headIndent(paragraphAt(*task, 7)) - std::ceil(width)) <= 0.5);
        const auto quote = concealed("> quote");
        QCOMPARE(S::headIndent(paragraphAt(*quote, 3)), S::firstLineHeadIndent(paragraphAt(*quote, 3)));
    }

    void incrementalConcealingMatchesFull()
    {
        const auto doc = concealed(document);
        const TextRange revealing(2, 0);
        replaceText(*doc, TextRange(2, 0), "**N**");
        S::apply(doc.get(), TextRange(2, 5), palette, S::defaultFontSize, true, revealing);
        QTextDocument full;
        full.setPlainText(doc->toPlainText());
        S::apply(&full, std::nullopt, palette, S::defaultFontSize, true, revealing);
        QString why;
        QVERIFY2(sameStyling(*doc, full, &why), qPrintable(why));
    }

    // MARK: Presentation

    void presentationRemovesMarkup()
    {
        const auto text = S::presentation(
            QStringLiteral("# Title\nSome **bold** and [a link](u)\n- item\n- [x] done\n> quote\n```\ncode\n```\n---"), palette);
        const QString expected = QStringLiteral("Title\nSome bold and a link\n• item\n☑ done\nquote\n\ncode\n\n ");
        QCOMPARE(text->toPlainText(), expected);
        QVERIFY(isBold(fontAt(*text, at(expected, "bold"))));
        QCOMPARE(colorAt(*text, at(expected, "a link")), palette.accent);
        QVERIFY(S::formatAt(*text, int(expected.size()) - 1).boolProperty(S::ruleProperty));
        const double bulletIndent = S::headIndent(paragraphAt(*text, at(expected, "item")));
        const double bulletWidth = QFontMetricsF(fonts::system(15)).horizontalAdvance(QStringLiteral("• "));
        QVERIFY(std::abs(bulletIndent - std::ceil(bulletWidth)) <= 0.5);
    }
};

QTEST_MAIN(MarkdownStylerTests)
#include "MarkdownStylerTests.moc"
