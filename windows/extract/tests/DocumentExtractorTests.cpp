#include "extract/Extraction.h"

#include <QUuid>
#include <QtTest>

using namespace wp;

class DocumentExtractorTests : public QObject {
    Q_OBJECT

private slots:
    void rejectsUnsupportedExtensions() {
        for (const QString name : {"notes.txt", "sheet.xlsx", "deck.key", "noextension"}) {
            bool threw = false;
            try {
                DocumentExtractor::extract(QStringLiteral("C:/tmp/") + name);
            } catch (const ExtractionError &e) {
                threw = true;
                QVERIFY(e == ExtractionError::unsupportedType(name));
            }
            QVERIFY(threw);
        }
    }

    void extensionsAreCaseInsensitive() {
        const QString file = QStringLiteral("does-not-exist-%1.PDF").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        bool threw = false;
        try {
            DocumentExtractor::extract(QStringLiteral("C:/tmp/") + file);
        } catch (const ExtractionError &e) {
            threw = true;
            QVERIFY(e == ExtractionError::unreadable(file));
        }
        QVERIFY(threw);
    }

    void errorDescriptionsMatchTheSwiftWording() {
        QCOMPARE(ExtractionError::unsupportedType("a.txt").description(), QString("a.txt: unsupported file type (use PDF, DOCX, DOC or PPTX)"));
        QCOMPARE(ExtractionError::unreadable("a.pdf").description(), QString("a.pdf: can't be read"));
        QCOMPARE(ExtractionError::empty("a.pdf").description(), QString("a.pdf: no text found"));
    }
};

QTEST_APPLESS_MAIN(DocumentExtractorTests)
#include "DocumentExtractorTests.moc"
