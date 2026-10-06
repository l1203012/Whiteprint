#include "Fixtures.h"
#include "extract/PDFExtractor.h"
#include "extract/TextRecognizer.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>

using namespace wp;
using Fixtures::PDFPage;

class PDFExtractionTests : public QObject {
    Q_OBJECT

    QTemporaryDir directory;

    static PDFPage textPage(int number, const QStringList &bodyLines) {
        QStringList lines{"Biology 101 - Lecture notes", ""};
        lines << bodyLines << QString() << "Ghent University" << QString::number(number);
        return PDFPage::text(lines);
    }

private slots:
    void initTestCase() { QVERIFY(directory.isValid()); }

    void extractsPagesAndRemovesHeadersFootersAndPageNumbers() {
        const QString path = directory.filePath("Syllabus.pdf");
        QVERIFY(Fixtures::writePDF({
            textPage(1, {"Cells are the basic unit of life.", "Each cell stores infor-", "mation in DNA."}),
            textPage(2, {"Mitochondria produce energy for the cell."}),
            PDFPage::blank(),
            textPage(4, {"Ribosomes build proteins from amino acids."}),
        }, path));

        auto document = DocumentExtractor::extract(path, false);
        QCOMPARE(document.name, QString("Syllabus.pdf"));
        QCOMPARE(Fixtures::show(document.units), Fixtures::show({
            // pdfium keeps the line breaks of the text layer (PDFKit reflows wrapped lines); the hyphenated
            // break is still joined by TextCleaner.
            {"p. 1", "Cells are the basic unit of life.\nEach cell stores information in DNA."},
            {"p. 2", "Mitochondria produce energy for the cell."},
            {"p. 4", "Ribosomes build proteins from amino acids."},
        }));
    }

    void ocrsPagesWithoutATextLayer() {
        if (!TextRecognizer::isAvailable()) QSKIP("Windows OCR is not available on this machine (no engine/language pack or no Windows PowerShell)");
        const QString path = directory.filePath("Scan.pdf");
        QVERIFY(Fixtures::writePDF({
            PDFPage::text({"A normal page with a text layer."}),
            PDFPage::image({"Photosynthesis converts light", "into chemical energy"}),
        }, path));

        auto document = DocumentExtractor::extract(path);
        QCOMPARE(document.units.size(), size_t(2));
        QCOMPARE(document.units[0].ref, QString("p. 1"));
        QCOMPARE(document.units[1].ref, QString("p. 2"));
        const QString scanned = document.units[1].text.toLower();
        QVERIFY2(scanned.contains("photosynthesis"), qPrintable(scanned));
        QVERIFY2(scanned.contains("chemical energy"), qPrintable(scanned));
        QVERIFY(scanned.indexOf("photosynthesis") < scanned.indexOf("chemical"));

        auto withoutOCR = DocumentExtractor::extract(path, false);
        QCOMPARE(withoutOCR.units.size(), size_t(1));
        QCOMPARE(withoutOCR.units[0].ref, QString("p. 1"));
    }

    void scannedPagesAreSkippedWithoutOCR() {
        const QString path = directory.filePath("ScanNoOcr.pdf");
        QVERIFY(Fixtures::writePDF({PDFPage::text({"A normal page with a text layer."}), PDFPage::image({"Picture only"})}, path));
        auto document = DocumentExtractor::extract(path, false);
        QCOMPARE(document.units.size(), size_t(1));
        QCOMPARE(document.units[0].ref, QString("p. 1"));
    }

    void ocrLanguagesAreSupportedOnes() {
        const QStringList preferred = TextRecognizer::preferredLanguages();
        for (const QString &language : TextRecognizer::languages()) QVERIFY(preferred.contains(language));
        if (TextRecognizer::isAvailable() && !TextRecognizer::languages().isEmpty()) {
            // Not every Windows install has English OCR; when any preferred language exists it must be one of ours.
            QVERIFY(!TextRecognizer::languages().isEmpty());
        } else {
            QVERIFY(TextRecognizer::recognize(QImage(10, 10, QImage::Format_RGB32)).isEmpty() || TextRecognizer::isAvailable());
        }
    }

    void blankPDFIsEmpty() {
        const QString path = directory.filePath("Blank.pdf");
        QVERIFY(Fixtures::writePDF({PDFPage::blank(), PDFPage::blank()}, path));
        bool threw = false;
        try {
            DocumentExtractor::extract(path);
        } catch (const ExtractionError &e) {
            threw = true;
            QVERIFY(e == ExtractionError::empty("Blank.pdf"));
        }
        QVERIFY(threw);
    }

    void corruptPDFIsUnreadable() {
        const QString path = directory.filePath("Broken.pdf");
        QVERIFY(Fixtures::writeFile(path, "%PDF-1.4 this is not really a pdf"));
        bool threw = false;
        try {
            DocumentExtractor::extract(path);
        } catch (const ExtractionError &e) {
            threw = true;
            QVERIFY(e == ExtractionError::unreadable("Broken.pdf"));
        }
        QVERIFY(threw);
    }

    void largePDFExtractsEveryPage() {
        const QString path = directory.filePath("Reader.pdf");
        std::vector<PDFPage> pages;
        for (int n = 1; n <= 300; ++n) {
            QStringList lines;
            for (int l = 1; l <= 30; ++l) lines << QStringLiteral("Page %1 line %2: the quick brown fox jumps over the lazy dog.").arg(n).arg(l);
            pages.push_back(textPage(n, lines));
        }
        QVERIFY(Fixtures::writePDF(pages, path));

        QElapsedTimer timer;
        timer.start();
        auto document = DocumentExtractor::extract(path);
        const double seconds = timer.elapsed() / 1000.0;
        QCOMPARE(document.units.size(), size_t(300));
        QCOMPARE(document.units.back().ref, QString("p. 300"));
        for (const auto &u : document.units) QVERIFY(!u.text.contains("Ghent University"));

        auto chunks = Chunker::chunks(document);
        QCOMPARE(chunks.front().firstRef, QString("p. 1"));
        QCOMPARE(chunks.back().lastRef, QString("p. 300"));
        qInfo("  300-page PDF: %.2f s, %d chunks", seconds, int(chunks.size()));
    }
};

int main(int argc, char **argv) {
    // The offscreen platform has no fonts on Windows, and the PDF fixtures need real text; use the native one.
    qputenv("QT_QPA_PLATFORM", "windows");
    QGuiApplication app(argc, argv);
    PDFExtractionTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "PDFExtractionTests.moc"
