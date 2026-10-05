#include "Fixtures.h"
#include "extract/RichTextExtractor.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace wp;
using Fixtures::body;
using Fixtures::heading;

class WordExtractionTests : public QObject {
    Q_OBJECT

    QTemporaryDir directory;
    const QString intro = "Course overview and grading. The exam counts for seventy percent of the final grade and the project for thirty percent.";

    static QStringList refs(const std::vector<ExtractedUnit> &units) {
        QStringList out;
        for (const auto &u : units) out << u.ref;
        return out;
    }

private slots:
    void initTestCase() { QVERIFY(directory.isValid()); }

    void splitsDOCXIntoSectionsAtHeadings() {
        const QString path = directory.filePath("Course.docx");
        QVERIFY(Fixtures::writeDocx({
            body(intro),
            heading("Chapter 1"),
            heading("1.1 The cell"),
            body("Cells are the basic unit of life."),
            body("They divide by mitosis."),
            heading("Chapter 2: A long heading about the many shapes and structures of proteins"),
            body("Proteins fold into shapes."),
        }, path));

        auto document = DocumentExtractor::extract(path);
        QCOMPARE(refs(document.units), (QStringList{
            "section 1",
            QStringLiteral("§ Chapter 1"),
            QStringLiteral("§ Chapter 2: A long heading about the many shapes and structu…"),
        }));
        QCOMPARE(document.units[1].text, QString("Chapter 1\n1.1 The cell\nCells are the basic unit of life.\nThey divide by mitosis."));
        QVERIFY(document.units[2].text.endsWith("\nProteins fold into shapes."));
    }

    void shortLineBeforeLongBodyCountsAsHeading() {
        QString lon;
        for (int i = 0; i < 5; ++i) lon += "Body text that explains the topic in detail. ";
        auto paragraphs = RichTextExtractor::paragraphs({
            {"Introduction", false, 12}, {lon.trimmed(), false, 12}, {"- a list item", false, 12}, {"short", false, 12}});
        QCOMPARE(paragraphs.size(), size_t(4));
        QVERIFY(paragraphs[0].isHeading);
        QVERIFY(!paragraphs[1].isHeading);
        QVERIFY(!paragraphs[2].isHeading);
        QVERIFY(!paragraphs[3].isHeading);
    }

    void documentWithoutHeadingsUsesNumberedSections() {
        const QString path = directory.filePath("Notes.docx");
        QString paragraph;
        for (int i = 0; i < 20; ++i) paragraph += "Plain text without any headings at all, just sentences. ";
        std::vector<Fixtures::WordPart> parts;
        for (int i = 0; i < 10; ++i) parts.push_back(body(paragraph));
        QVERIFY(Fixtures::writeDocx(parts, path));

        auto document = DocumentExtractor::extract(path);
        QVERIFY(document.units.size() > 1);
        QStringList expected;
        for (size_t i = 0; i < document.units.size(); ++i) expected << QStringLiteral("section %1").arg(i + 1);
        QCOMPARE(refs(document.units), expected);
        for (const auto &u : document.units) QVERIFY(u.text.size() <= RichTextExtractor::maxSectionLength);
    }

    void longSectionIsSplitIntoParts() {
        std::vector<RichTextExtractor::Paragraph> paragraphs{{"Methods", true}};
        for (int i = 1; i <= 5; ++i) paragraphs.push_back({QString(2500, QChar('0' + i)), false});
        QCOMPARE(refs(RichTextExtractor::sections(paragraphs)),
                 (QStringList{QStringLiteral("§ Methods"), QStringLiteral("§ Methods, part 2"), QStringLiteral("§ Methods, part 3")}));
    }

    void wordHeadingStylesAreHeadingsEvenWithoutBold() {
        const QByteArray styles =
            "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
            "<w:docDefaults><w:rPrDefault><w:rPr><w:sz w:val=\"22\"/></w:rPr></w:rPrDefault></w:docDefaults>"
            "<w:style w:styleId=\"H1\"><w:name w:val=\"heading 1\"/></w:style>"
            "<w:style w:styleId=\"Big\"><w:name w:val=\"Big\"/><w:rPr><w:b/><w:sz w:val=\"40\"/></w:rPr></w:style></w:styles>";
        const QByteArray document =
            "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
            "<w:p><w:pPr><w:pStyle w:val=\"H1\"/></w:pPr><w:r><w:t>Overview.</w:t></w:r></w:p>"
            "<w:p><w:pPr><w:pStyle w:val=\"Big\"/></w:pPr><w:r><w:t>Styled</w:t></w:r><w:r><w:tab/><w:t>title</w:t></w:r></w:p>"
            "<w:p><w:r><w:t>Plain body.</w:t></w:r></w:p></w:body></w:document>";
        auto raw = RichTextExtractor::docxParagraphs(document, styles);
        QCOMPARE(raw.size(), size_t(3));
        QVERIFY(raw[0].headingStyle);
        QCOMPARE(raw[1].text, QString("Styled title"));
        QVERIFY(raw[1].bold);
        QCOMPARE(raw[1].size, 20.0);
        QVERIFY(!raw[2].bold);
        QCOMPARE(raw[2].size, 11.0);
    }

    void readsLegacyDOCSavedAsRTF() {
        const QString path = directory.filePath("Old.doc");
        QVERIFY(Fixtures::writeFile(path, Fixtures::rtfDocument({heading("Summary"), body("Legacy Word files still work.")})));
        auto document = DocumentExtractor::extract(path);
        QCOMPARE(document.units.size(), size_t(1));
        QCOMPARE(document.units[0].ref, QStringLiteral("§ Summary"));
        QCOMPARE(document.units[0].text, QString("Summary\nLegacy Word files still work."));
    }

    void readsBinaryWord97DOC() {
        // The binary reader has no formatting, so the heading is recognised as a short line before a long paragraph.
        QString lon;
        for (int i = 0; i < 4; ++i) lon += "Legacy Word files still work and their text is read from the piece table. ";
        const QString path = directory.filePath("Binary.doc");
        QVERIFY(Fixtures::writeFile(path, Fixtures::binaryDoc({heading("Summary"), body(lon.trimmed()), body("Second paragraph.")})));
        auto document = DocumentExtractor::extract(path);
        QCOMPARE(document.units.size(), size_t(1));
        QCOMPARE(document.units[0].ref, QStringLiteral("§ Summary"));
        QCOMPARE(document.units[0].text, "Summary\n" + lon.trimmed() + "\nSecond paragraph.");
    }

    void rtfEscapesAreDecoded() {
        auto raw = RichTextExtractor::rtfParagraphs("{\\rtf1\\ansi{\\fonttbl{\\f0 Arial;}}\\f0 Caf\\'e9 \\u8364? {\\*\\generator Hidden;}costs 5\\par Next\\par}");
        QCOMPARE(raw.size(), size_t(2));
        QCOMPARE(raw[0].text, QStringLiteral("Café € costs 5"));
        QCOMPARE(raw[1].text, QString("Next"));
    }

    void corruptDOCXIsUnreadable() {
        const QString path = directory.filePath("Broken.docx");
        QVERIFY(Fixtures::writeFile(path, "PK not really a docx"));
        bool threw = false;
        try {
            DocumentExtractor::extract(path);
        } catch (const ExtractionError &e) {
            threw = true;
            QVERIFY(e == ExtractionError::unreadable("Broken.docx"));
        }
        QVERIFY(threw);
    }
};

QTEST_APPLESS_MAIN(WordExtractionTests)
#include "WordExtractionTests.moc"
