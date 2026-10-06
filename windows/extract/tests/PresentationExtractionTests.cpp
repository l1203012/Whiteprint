#include "Fixtures.h"
#include "extract/PresentationExtractor.h"
#include "extract/PresentationXML.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace wp;
using Fixtures::shape;
using Fixtures::slideXML;

#define EXPECT_EXTRACTION_ERROR(expr, expected)                       \
    do {                                                              \
        bool threw_ = false;                                          \
        try {                                                         \
            expr;                                                     \
        } catch (const ExtractionError &e_) {                         \
            threw_ = true;                                            \
            QVERIFY2(e_ == (expected), e_.what());                    \
        }                                                             \
        QVERIFY2(threw_, "expected an ExtractionError");              \
    } while (0)

class PresentationExtractionTests : public QObject {
    Q_OBJECT

    QTemporaryDir directory;

    QString deck(const QString &name, QMap<QString, QString> parts, const QSet<QString> &stored = {}) {
        if (!parts.contains("[Content_Types].xml")) parts.insert("[Content_Types].xml", Fixtures::contentTypes);
        const QString path = directory.filePath(name);
        if (!Fixtures::writeZip(parts, stored, path)) qFatal("can't write %s", qPrintable(path));
        return path;
    }
    static std::vector<ExtractedUnit> units(std::initializer_list<std::pair<QString, QString>> l) {
        std::vector<ExtractedUnit> v;
        for (auto &p : l) v.push_back({p.first, p.second});
        return v;
    }

private slots:
    void initTestCase() { QVERIFY(directory.isValid()); }

    void readsTitlesFirstBodyLinesAndSpeakerNotes() {
        const QString url = deck("Lecture3.pptx", {
            {"ppt/slides/slide1.xml", slideXML({
                shape("body", {"Glycolysis splits glucose", "It runs in the |cytoplasm"}),
                shape("title", {"Cell |respiration"}),
                shape("sldNum", {"1"}),
                shape("ftr", {"Biology 101"}),
            })},
            {"ppt/slides/_rels/slide1.xml.rels", Fixtures::notesRels("../notesSlides/notesSlide1.xml")},
            {"ppt/notesSlides/notesSlide1.xml", slideXML({
                shape("sldImg", {}),
                shape("body", {"Stress that ATP is the &amp; key output.", "Mention the exam."}),
                shape("sldNum", {"1"}),
            }, "notes")},
            {"ppt/slides/slide2.xml", slideXML({shape(QString(), {"A free text box"})})},
        });
        auto document = DocumentExtractor::extract(url);
        QCOMPARE(Fixtures::show(document.units), Fixtures::show(units({
            {"slide 1", "Cell respiration\nGlycolysis splits glucose\nIt runs in the cytoplasm\n\nNotes: Stress that ATP is the & key output.\nMention the exam."},
            {"slide 2", "A free text box"},
        })));
    }

    void slidesWithoutPresentationPartAreInNumericOrder() {
        QMap<QString, QString> parts;
        for (int n : {1, 2, 10}) parts.insert(QStringLiteral("ppt/slides/slide%1.xml").arg(n), slideXML({shape("title", {QStringLiteral("Slide file %1").arg(n)})}));
        auto document = DocumentExtractor::extract(deck("Numeric.pptx", parts));
        QCOMPARE(document.units.size(), size_t(3));
        QCOMPARE(document.units[0].text, QString("Slide file 1"));
        QCOMPARE(document.units[1].text, QString("Slide file 2"));
        QCOMPARE(document.units[2].text, QString("Slide file 10"));
        QCOMPARE(document.units[2].ref, QString("slide 3"));
    }

    void presentationPartDefinesSlideOrder() {
        auto parts = Fixtures::presentationParts({"slide2.xml", "slide1.xml"});
        parts["ppt/slides/slide1.xml"] = slideXML({shape("title", {"Moved to the end"})});
        parts["ppt/slides/slide2.xml"] = slideXML({shape("title", {"Now first"})});
        auto document = DocumentExtractor::extract(deck("Reordered.pptx", parts));
        QCOMPARE(Fixtures::show(document.units), Fixtures::show(units({{"slide 1", "Now first"}, {"slide 2", "Moved to the end"}})));
    }

    void readsStoredSlideEntries() {
        const QString url = deck("Stored.pptx", {
            {"ppt/slides/slide1.xml", slideXML({shape("title", {"Deflated slide"})})},
            {"ppt/slides/slide2.xml", slideXML({shape("title", {"Stored slide"})})},
        }, {"ppt/slides/slide2.xml"});
        QCOMPARE(int(ZipArchive(url).entry("ppt/slides/slide2.xml")->method), 0);
        auto document = DocumentExtractor::extract(url);
        QCOMPARE(document.units.size(), size_t(2));
        QCOMPARE(document.units[0].text, QString("Deflated slide"));
        QCOMPARE(document.units[1].text, QString("Stored slide"));
    }

    void removesTextRepeatedOnEverySlide() {
        QMap<QString, QString> parts;
        const QStringList letters{"A", "B", "C", "D"};
        for (int n = 1; n <= 4; ++n)
            parts.insert(QStringLiteral("ppt/slides/slide%1.xml").arg(n), slideXML({
                shape("title", {"Topic " + letters[n - 1]}),
                shape(QString(), {QStringLiteral("Point for slide %1").arg(n)}),
                shape(QString(), {QStringLiteral("© 2026 Ghent University")}),
            }));
        auto document = DocumentExtractor::extract(deck("Footer.pptx", parts));
        QCOMPARE(document.units.size(), size_t(4));
        for (int n = 1; n <= 4; ++n)
            QCOMPARE(document.units[n - 1].text, QStringLiteral("Topic %1\nPoint for slide %2").arg(letters[n - 1]).arg(n));
    }

    void deckWithoutTextIsEmpty() {
        const QString url = deck("Pictures.pptx", {{"ppt/slides/slide1.xml", slideXML({shape("sldNum", {"1"})})}});
        EXPECT_EXTRACTION_ERROR(DocumentExtractor::extract(url), ExtractionError::empty("Pictures.pptx"));
    }

    void zipWithoutSlidesOrMalformedXMLIsUnreadable() {
        const QString noSlides = deck("NoSlides.pptx", {{"docProps/app.xml", "<x/>"}});
        EXPECT_EXTRACTION_ERROR(DocumentExtractor::extract(noSlides), ExtractionError::unreadable("NoSlides.pptx"));
        const QString malformed = deck("Malformed.pptx", {{"ppt/slides/slide1.xml", "<p:sld><unclosed>"}});
        EXPECT_EXTRACTION_ERROR(DocumentExtractor::extract(malformed), ExtractionError::unreadable("Malformed.pptx"));
        const QString notZip = directory.filePath("Fake.pptx");
        QVERIFY(Fixtures::writeFile(notZip, "hello"));
        EXPECT_EXTRACTION_ERROR(DocumentExtractor::extract(notZip), ExtractionError::unreadable("Fake.pptx"));
    }

    void ignoresFallbackCopiesOfAlternateContent() {
        const QString xml = QStringLiteral(
            "<p:sld xmlns:a=\"%1\" xmlns:p=\"%2\" xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\">"
            "<p:cSld><p:spTree><mc:AlternateContent><mc:Choice Requires=\"x\">%3</mc:Choice>"
            "<mc:Fallback>%3</mc:Fallback></mc:AlternateContent></p:spTree></p:cSld></p:sld>")
                                .arg(Fixtures::drawingNS, Fixtures::presentationNS, shape(QString(), {"Once"}));
        QStringList paragraphs;
        for (const auto &s : ShapeTextParser::parse(xml.toUtf8())) paragraphs << s.paragraphs;
        QCOMPARE(paragraphs, QStringList{"Once"});
    }
};

QTEST_APPLESS_MAIN(PresentationExtractionTests)
#include "PresentationExtractionTests.moc"
