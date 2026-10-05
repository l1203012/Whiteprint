#include "Bitmap.h"
#include "render/PDFExporter.h"

#include <QBuffer>
#include <QLocale>
#include <QPdfDocument>
#include <QtTest>
#include <cmath>
#include <memory>

using namespace wp;
using namespace wp::testing;

namespace {

const QDateTime testDate = QDateTime::fromSecsSinceEpoch(1790000000);

struct LoadedPDF {
    QByteArray bytes;
    QBuffer buffer;
    mutable QPdfDocument document;

    explicit LoadedPDF(const QByteArray &data) : bytes(data), buffer(&bytes)
    {
        buffer.open(QIODevice::ReadOnly);
        document.load(&buffer);
    }

    bool ok() const { return document.status() == QPdfDocument::Status::Ready; }
    int pageCount() const { return document.pageCount(); }
    QString text(int page) const { return document.getAllText(page).text(); }
    QString allText() const
    {
        QStringList parts;
        for (int i = 0; i < pageCount(); ++i)
            parts.append(text(i));
        return parts.join(QLatin1Char('\n'));
    }
    Bitmap render(int page) const
    {
        const QSizeF size = document.pagePointSize(page);
        return Bitmap(document.render(page, QSize(int(size.width()), int(size.height()))));
    }
};

std::unique_ptr<LoadedPDF> pdf(const Note &note, PDFExportStyle style = PDFExportStyle::blueprint)
{
    return std::make_unique<LoadedPDF>(PDFExporter::data(note, style, testDate));
}

QString longText(int lines)
{
    QStringList l;
    for (int i = 1; i <= lines; ++i)
        l.append(QStringLiteral("- item %1 with a few words in it").arg(i));
    return l.join(QLatin1Char('\n'));
}

QString stripped(const QString &s)
{
    QString out = s;
    out.remove(QChar(' ')).remove(QChar('\n')).remove(QChar('\r'));
    return out;
}

} // namespace

class PDFExporterTests : public QObject
{
    Q_OBJECT

private slots:
    void paperSizeFollowsTheRegion()
    {
        QCOMPARE(PDFExporter::paperSize(QStringLiteral("US")), PDFExporter::letter);
        QCOMPARE(PDFExporter::paperSize(QStringLiteral("CA")), PDFExporter::letter);
        QCOMPARE(PDFExporter::paperSize(QStringLiteral("DE")), PDFExporter::a4);
        QCOMPARE(PDFExporter::paperSize(std::nullopt), PDFExporter::a4);
    }

    void pagesUseThePaperSize()
    {
        const auto doc = pdf(Note(QStringLiteral("T")));
        QVERIFY(doc->ok());
        const QLocale locale;
        const std::optional<QString> region = locale.territory() == QLocale::AnyTerritory
                                                  ? std::nullopt
                                                  : std::optional<QString>(QLocale::territoryToCode(locale.territory()));
        const QSizeF expected = PDFExporter::paperSize(region);
        const QSizeF actual = doc->document.pagePointSize(0);
        QVERIFY(std::abs(actual.width() - expected.width()) < 1.5);
        QVERIFY(std::abs(actual.height() - expected.height()) < 1.5);
    }

    void emptyNoteIsOnePage()
    {
        const auto doc = pdf(Note());
        QVERIFY(doc->ok());
        QCOMPARE(doc->pageCount(), 1);
    }

    void eachNotePageStartsAPDFPage()
    {
        const Note note(QStringLiteral("Course"), {NotePage({NoteBlock::textBlock("# First\nshort")}),
                                                   NotePage({NoteBlock::textBlock("# Second\nshort")})});
        // Print pages, since blueprint pages repeat the title in their title block.
        const auto doc = pdf(note, PDFExportStyle::print);
        QVERIFY(doc->ok());
        QCOMPARE(doc->pageCount(), 2);
        QVERIFY2(doc->text(0).trimmed().startsWith(QStringLiteral("Course")), "title on the first page");
        QVERIFY(doc->text(1).trimmed().startsWith(QStringLiteral("Second")));
        QVERIFY(!doc->text(1).contains(QStringLiteral("Course")));
    }

    void longPagesFlowOverSeveralPDFPages()
    {
        const Note note(QStringLiteral("Long"), {NotePage({NoteBlock::textBlock("intro")}), NotePage({NoteBlock::textBlock(longText(150))})});
        const auto doc = pdf(note);
        QVERIFY(doc->ok());
        QVERIFY(doc->pageCount() >= 4);
        // pdfium sometimes extracts a densely stacked page one glyph per line, so compare without whitespace.
        const QString text = stripped(doc->allText());
        QVERIFY(text.contains(QStringLiteral("item1with")));
        QVERIFY(text.contains(QStringLiteral("item150with")));
    }

    void drawingsAndMarkupAreLeftOut()
    {
        const Note note(QStringLiteral("T"), {NotePage({NoteBlock::textBlock("Some **bold** words"),
                                                        NoteBlock::drawingBlock(Drawing("d1", "flow Zebra>Yak"))})});
        const auto doc = pdf(note);
        QVERIFY(doc->ok());
        const QString text = doc->text(0);
        QVERIFY(text.contains(QStringLiteral("Some bold words")));
        QVERIFY(!text.contains(QStringLiteral("Zebra")));
        QVERIFY(!text.contains(QStringLiteral("**")));
    }

    void drawingOnlyPagesAreSkipped()
    {
        const Note note({NotePage({NoteBlock::textBlock("one")}), NotePage({NoteBlock::drawingBlock(Drawing("d1", "box a"))}),
                         NotePage({NoteBlock::textBlock("three")})});
        QCOMPARE(pdf(note)->pageCount(), 2);
    }

    void titleIsNotRepeatedWhenThePageStartsWithIt()
    {
        const Note note(QStringLiteral("Plan"), {NotePage({NoteBlock::textBlock("# Plan\nbody")})});
        const auto doc = pdf(note, PDFExportStyle::print);
        QVERIFY(doc->ok());
        QCOMPARE(doc->text(0).count(QStringLiteral("Plan")), 1);
    }

    void stylesPaintTheirPaper()
    {
        const auto blueprintDoc = pdf(Note(QStringLiteral("T")));
        const auto printDoc = pdf(Note(QStringLiteral("T")), PDFExportStyle::print);
        QVERIFY(blueprintDoc->ok() && printDoc->ok());
        const Bitmap blueprint = blueprintDoc->render(0);
        const Bitmap print = printDoc->render(0);
        const int middle = blueprint.height() / 2;
        QVERIFY(blueprint.pixelMatches(10, middle, BlueprintPalette::blueprint().pageBackground, 16));
        const Pixel corner = blueprint.pixel(blueprint.width() - 3, blueprint.height() - 3);
        QVERIFY2(corner.b > corner.r + 40, "vignetted corners stay blue");
        QVERIFY(print.pixelMatches(5, 5, Qt::white, 4));
        QVERIFY(print.pixelMatches(print.width() - 5, print.height() - 5, Qt::white, 4));
    }

    void blueprintPagesHaveATitleBlockAndPrintPagesANumber()
    {
        const Note note(QStringLiteral("Survey"), {NotePage({NoteBlock::textBlock("one")}), NotePage({NoteBlock::textBlock("two")})});
        const auto blueprint = pdf(note);
        QVERIFY(blueprint->ok());
        const QString last = stripped(blueprint->text(1));
        QVERIFY(last.contains(QStringLiteral("Survey")));
        QVERIFY2(last.contains(QStringLiteral("2/2")), "page n / N");
        QVERIFY(last.contains(testDate.toLocalTime().date().toString(QStringLiteral("yyyy-MM-dd"))));
        QVERIFY(last.contains(QStringLiteral("WHITEPRINT")));
        const auto print = pdf(note, PDFExportStyle::print);
        const QString printText = print->text(1).trimmed();
        QVERIFY(!printText.contains(QStringLiteral("WHITEPRINT")));
        QVERIFY(printText.endsWith(QLatin1Char('2')));
    }

    /// Viewers split copied text into fragments when hundreds of stroked
    /// lines sit under it, which the grid pattern avoids.
    void bodyTextCopiesOutWhole()
    {
        const QString sentence = QStringLiteral("The deck carries a uniform load of four kilonewtons per metre over the full span");
        const Note note(QStringLiteral("Copy"), {NotePage({NoteBlock::textBlock(sentence + QStringLiteral(".\n\n") + longText(10))})});
        const auto doc = pdf(note);
        QVERIFY(doc->ok());
        const QString text = doc->text(0);
        QVERIFY2(text.trimmed().startsWith(QStringLiteral("Copy")), "page text comes before the title block");
        QString flat = text;
        flat.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QStringLiteral("\r"), QString());
        QVERIFY(flat.contains(sentence));
        for (int line = 1; line <= 10; ++line)
            QVERIFY2(text.contains(QStringLiteral("item %1 with a few words in it").arg(line)), qPrintable(QString::number(line)));
    }

    void blueprintPagesStaySmall()
    {
        auto note = [](int pages) {
            QList<NotePage> list;
            for (int i = 1; i <= pages; ++i)
                list.append(NotePage({NoteBlock::textBlock(QStringLiteral("# Page %1\nsome words").arg(i))}));
            return Note(QStringLiteral("Size"), list);
        };
        const qsizetype one = PDFExporter::data(note(1), PDFExportStyle::blueprint, testDate).size();
        const qsizetype ten = PDFExporter::data(note(10), PDFExportStyle::blueprint, testDate).size();
        QVERIFY2(one < 60000, qPrintable(QString::number(one)));
        QVERIFY2((ten - one) / 9 < 6000, qPrintable(QString::number((ten - one) / 9)));
    }
};

QTEST_MAIN(PDFExporterTests)
#include "PDFExporterTests.moc"
