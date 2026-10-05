#include "render/MarkdownSyntax.h"

#include <QtTest>

using namespace wp;

class MarkdownSyntaxTests : public QObject
{
    Q_OBJECT

private:
    static MarkdownLineKind kind(const QString &line)
    {
        return MarkdownSyntax::classify(line, std::nullopt).line.kind;
    }
    static MarkdownLine classified(const QString &line)
    {
        return MarkdownSyntax::classify(line, std::nullopt).line;
    }
    static int marker(const QString &line) { return classified(line).markerLength; }

    static bool isHeading(const QString &line, int level)
    {
        const MarkdownLine l = classified(line);
        return l.kind == MarkdownLineKind::heading && l.level == level;
    }
    static bool isTask(const QString &line, bool checked)
    {
        const MarkdownLine l = classified(line);
        return l.kind == MarkdownLineKind::task && l.checked == checked;
    }

    static QStringList spans(const QString &text)
    {
        QStringList out;
        for (const MarkdownSpan &s : MarkdownSyntax::spans(text)) {
            static const char *names[] = {"bold", "italic", "code", "link"};
            out.append(QString::fromLatin1(names[int(s.kind)]) + QLatin1Char(':') + text.mid(s.contentRange.location, s.contentRange.length));
        }
        return out;
    }
    static QStringList sorted(QStringList l)
    {
        l.sort();
        return l;
    }

private slots:
    void headings()
    {
        QVERIFY(isHeading("# Title", 1));
        QVERIFY(isHeading("### Sub", 3));
        QCOMPARE(marker("## Two"), 3);
        QCOMPARE(kind("#hashtag"), MarkdownLineKind::paragraph);
        QCOMPARE(kind("####### seven"), MarkdownLineKind::paragraph);
        QCOMPARE(kind("    # indented code"), MarkdownLineKind::paragraph);
    }

    void lists()
    {
        QCOMPARE(kind("- item"), MarkdownLineKind::bullet);
        QCOMPARE(kind("* item"), MarkdownLineKind::bullet);
        QCOMPARE(kind("  + nested"), MarkdownLineKind::bullet);
        QCOMPARE(marker("  - nested"), 4);
        QCOMPARE(kind("12. twelve"), MarkdownLineKind::ordered);
        QCOMPARE(kind("3) three"), MarkdownLineKind::ordered);
        QCOMPARE(marker("10. ten"), 4);
        QCOMPARE(kind("-not a list"), MarkdownLineKind::paragraph);
        QCOMPARE(kind("2024.10 is a date"), MarkdownLineKind::paragraph);
    }

    void checklists()
    {
        QVERIFY(isTask("- [ ] open", false));
        QVERIFY(isTask("- [x] done", true));
        QVERIFY(isTask("* [X] done", true));
        QCOMPARE(marker("- [ ] open"), 6);
        QCOMPARE(kind("- [y] not a box"), MarkdownLineKind::bullet);
        QVERIFY(isTask("- [ ]", false));
    }

    void quotesAndDividers()
    {
        QCOMPARE(kind("> quoted"), MarkdownLineKind::quote);
        QCOMPARE(marker("> > nested"), 4);
        QCOMPARE(kind("---"), MarkdownLineKind::divider);
        QCOMPARE(kind("* * *"), MarkdownLineKind::divider);
        QCOMPARE(kind("___"), MarkdownLineKind::divider);
        QCOMPARE(kind("--"), MarkdownLineKind::paragraph);
        QCOMPARE(kind(""), MarkdownLineKind::blank);
        QCOMPARE(kind("   "), MarkdownLineKind::blank);
    }

    void fences()
    {
        const auto open = MarkdownSyntax::classify("```swift", std::nullopt);
        QCOMPARE(open.line.kind, MarkdownLineKind::fence);
        QVERIFY(open.openFence.has_value());
        const auto fence = open.openFence;
        QVERIFY(*fence == MarkdownFenceState(0x60, 3));

        QCOMPARE(MarkdownSyntax::classify("# not a heading", fence).line.kind, MarkdownLineKind::code);
        QVERIFY(MarkdownSyntax::classify("``", fence).openFence.has_value()); // too short to close
        QVERIFY(MarkdownSyntax::classify("~~~", fence).openFence.has_value()); // other marker
        const auto close = MarkdownSyntax::classify("````  ", fence);
        QCOMPARE(close.line.kind, MarkdownLineKind::fence);
        QVERIFY(!close.openFence.has_value());

        QVERIFY(!MarkdownSyntax::classify("``` a`b", std::nullopt).openFence.has_value()); // backtick in info string
        QVERIFY(MarkdownSyntax::classify("~~~~", std::nullopt).openFence.has_value());
    }

    void fenceStateRoundTripsThroughItsEncoding()
    {
        const MarkdownFenceState state(0x7E, 4);
        const auto back = MarkdownFenceState::fromEncoded(state.encoded());
        QVERIFY(back.has_value());
        QVERIFY(*back == state);
        QVERIFY(!MarkdownFenceState::fromEncoded(QString()).has_value());
    }

    // MARK: Inline

    void emphasis()
    {
        QCOMPARE(spans("a **bold** b"), QStringList{"bold:bold"});
        QCOMPARE(spans("__also__"), QStringList{"bold:also"});
        QCOMPARE(spans("*one* and _two_"), (QStringList{"italic:one", "italic:two"}));
        QCOMPARE(sorted(spans("***both***")), (QStringList{"bold:*both", "italic:both**"}));
        QCOMPARE(spans("snake_case_name"), QStringList());
        QCOMPARE(spans("2 * 3 * 4"), QStringList());
        QCOMPARE(spans("**unclosed"), QStringList());
        QCOMPARE(spans(R"(\*escaped\*)"), QStringList());
    }

    void codeSpansHideMarkup()
    {
        QCOMPARE(spans("use `**x**` here"), QStringList{"code:**x**"});
        QCOMPARE(spans("``a ` b``"), QStringList{"code:a ` b"});
        QCOMPARE(spans("`unclosed"), QStringList());
    }

    void links()
    {
        const auto found = MarkdownSyntax::spans("see [the docs](https://x.io/a_b_c) now", 10);
        QCOMPARE(found.size(), 1);
        QCOMPARE(int(found.first().kind), int(MarkdownSpan::Kind::link));
        QCOMPARE(found.first().url.value_or(QString()), QStringLiteral("https://x.io/a_b_c"));
        QVERIFY(found.first().contentRange == TextRange(15, 8));
        const QList<TextRange> expected{TextRange(14, 1), TextRange(23, 21)};
        QVERIFY(found.first().markupRanges() == expected);
        QCOMPARE(sorted(spans("[**bold link**](u)")), (QStringList{"bold:bold link", "link:**bold link**"}));
        QCOMPARE(spans("[not a link] (u)"), QStringList());
    }
};

QTEST_MAIN(MarkdownSyntaxTests)
#include "MarkdownSyntaxTests.moc"
