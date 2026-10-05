#include "Bitmap.h"
#include "render/PageThumbnail.h"

#include <QtTest>

using namespace wp;
using namespace wp::testing;

class PageThumbnailTests : public QObject
{
    Q_OBJECT

private:
    static NotePage samplePage()
    {
        return NotePage({
            NoteBlock::textBlock("# Overview\nSome **notes**.\n- [ ] todo"),
            NoteBlock::drawingBlock(Drawing("d1", "flow Client>API>DB")),
            NoteBlock::textBlock("More text."),
        });
    }

    static Bitmap render(const NotePage &page, QSize size, const BlueprintPalette &palette = BlueprintPalette::blueprint())
    {
        return Bitmap(PageThumbnail::image(page, size, palette));
    }

private slots:
    void imageHasTheRequestedSize()
    {
        QCOMPARE(PageThumbnail::image(samplePage(), QSize(90, 120)).deviceIndependentSize().toSize(), QSize(90, 120));
    }

    void drawsBackgroundAndContent()
    {
        const QSize size(160, 220);
        const BlueprintPalette palette = BlueprintPalette::blueprint();
        const Bitmap thumbnail = render(samplePage(), size);
        const Bitmap empty = render(NotePage(), size);
        QVERIFY(thumbnail.pixelMatches(1, 218, palette.pageBackground, 20));
        QCOMPARE(empty.count(palette.text, 60), 0);
        QVERIFY(thumbnail.count(palette.text, 120) > 100); // antialiased 5 px text is faint
    }

    void tinyThumbnailsStillShowSomething()
    {
        const Bitmap thumbnail = render(samplePage(), QSize(40, 56));
        const QColor background = BlueprintPalette::blueprint().pageBackground;
        int ink = 0;
        for (int y = 0; y < 56; ++y)
            for (int x = 0; x < 40; ++x)
                if (!thumbnail.pixelMatches(x, y, background, 25))
                    ++ink;
        QVERIFY(ink > 10);
    }

    void printPalette()
    {
        const Bitmap thumbnail = render(samplePage(), QSize(120, 160), BlueprintPalette::print());
        QVERIFY(thumbnail.pixelMatches(2, 158, Qt::white, 10));
    }

    void plainTextDropsHeadingMarkerAndInlineMarkup()
    {
        QCOMPARE(PageThumbnail::plainText("## A **b** `c` [d](e)", 3), QStringLiteral("A b c d"));
    }
};

QTEST_MAIN(PageThumbnailTests)
#include "PageThumbnailTests.moc"
