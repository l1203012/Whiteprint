#include "extract/TextCleaner.h"

#include <QtTest>

using namespace wp;

class TextCleanerTests : public QObject {
    Q_OBJECT

    static QStringList clean(const QStringList &texts) {
        std::vector<ExtractedUnit> units;
        for (int i = 0; i < texts.size(); ++i) units.push_back({QStringLiteral("p. %1").arg(i + 1), texts[i]});
        QStringList out;
        for (const auto &u : TextCleaner::clean(units)) out << u.text;
        return out;
    }
    static QString n(int v) { return QString::number(v); }

private slots:
    void collapsesWhitespaceAndBlankLines() {
        QCOMPARE(clean({QStringLiteral("  Cells \t are   small.\r\n\n\n\n Very small. \n\n")}), QStringList{"Cells are small.\n\nVery small."});
    }

    void joinsHyphenatedLineBreaks() {
        QCOMPARE(clean({"The infor-\nmation is stored in DNA-\nRNA pairs.\nEnd -\nof line"}),
                 QStringList{"The information is stored in DNA-\nRNA pairs.\nEnd -\nof line"});
        QCOMPARE(clean({QStringLiteral("soft­hyphen")}), QStringList{"softhyphen"});
    }

    void dropsPageNumberLines() {
        QCOMPARE(clean({"Intro\n12\n- 13 -\nPage 4\nPage 4 of 20\n3/40\nPagina 2 van 9\nSeite 5\nIn 1990 we had 12 cases"}),
                 QStringList{"Intro\nIn 1990 we had 12 cases"});
        QVERIFY(!TextCleaner::isPageNumber("Chapter 3"));
        QVERIFY(!TextCleaner::isPageNumber("3.2 Methods"));
        QVERIFY(TextCleaner::isPageNumber("p. 7"));
    }

    void removesRepeatedHeadersAndFooters() {
        QStringList pages, expected;
        for (int i = 1; i <= 4; ++i) {
            pages << QStringLiteral("BIO 101 — Cell Biology\nTopic %1 body text.\nMore about topic %1.\nUniversity of Ghent · %2").arg(i).arg(i + 10);
            expected << QStringLiteral("Topic %1 body text.\nMore about topic %1.").arg(i);
        }
        QCOMPARE(clean(pages), expected);
    }

    void keepsRepeatedLinesInTheMiddleOfAUnit() {
        const QStringList pages{"Alpha\nbeta\nSee figure\ngamma\ndelta", "Epsilon\nzeta\nSee figure\neta\ntheta", "Iota\nkappa\nSee figure\nlambda\nmu"};
        QCOMPARE(clean(pages), pages);
    }

    void ignoresDigitsOnlyOnTheOutermostLines() {
        QStringList pages, expected;
        for (int i = 1; i <= 3; ++i) {
            pages << QStringLiteral("Example %1\nStep %1 of the method\nDetails %1 and more\nResult %1").arg(i);
            expected << QStringLiteral("Step %1 of the method\nDetails %1 and more").arg(i);
        }
        QCOMPARE(clean(pages), expected);
    }

    void digitInsensitiveMatchingSkipsLongLinesAndNeverEmptiesAUnit() {
        QStringList lon;
        for (int i = 1; i <= 4; ++i) {
            QString line = QStringLiteral("Page %1 line: ").arg(i + 100);
            for (int k = 0; k < 5; ++k) line += "the quick brown fox ";
            lon << line + "end";
        }
        QCOMPARE(clean(lon), lon);
        QStringList titles;
        for (int i = 1; i <= 3; ++i) titles << QStringLiteral("Slide title %1").arg(i);
        QCOMPARE(clean(titles), titles);
    }

    void keepsLinesThatRepeatInHalfOrFewerUnits() {
        const QStringList pages{"Draft\nOne", "Draft\nTwo", "Three", "Four"};
        QCOMPARE(clean(pages), pages);
    }

    void needsThreeUnitsToDetectHeaders() {
        const QStringList pages{"Course\nOne", "Course\nTwo"};
        QCOMPARE(clean(pages), pages);
    }

    void dropsEmptyUnitsAndKeepsRefs() {
        auto units = TextCleaner::clean({{"p. 1", "One"}, {"p. 2", " \n 2 \n"}, {"p. 3", "Three"}});
        QVERIFY((units == std::vector<ExtractedUnit>{{"p. 1", "One"}, {"p. 3", "Three"}}));
    }
};

QTEST_APPLESS_MAIN(TextCleanerTests)
#include "TextCleanerTests.moc"
