#include "Bitmap.h"
#include "render/BlueprintBackground.h"

#include <QtTest>

using namespace wp;
using namespace wp::testing;

class BlueprintBackgroundTests : public QObject
{
    Q_OBJECT

private:
    const QSizeF size{200, 200};

    Bitmap render(const BlueprintBackground::Options &options, const BlueprintPalette &palette = BlueprintPalette::blueprint()) const
    {
        Bitmap bitmap(int(size.width()), int(size.height()));
        QPainter painter(&bitmap.image);
        BlueprintBackground::draw(painter, QRectF(QPointF(0, 0), size), palette, options);
        painter.end();
        return bitmap;
    }

private slots:
    void plainIsThePageColour()
    {
        const Bitmap bitmap = render(BlueprintBackground::Options::plain());
        const QColor background = BlueprintPalette::blueprint().pageBackground;
        QCOMPARE(bitmap.count(background, 1), int(size.width() * size.height()));
    }

    void gridHasMinorAndMajorLines()
    {
        const Bitmap bitmap = render(BlueprintBackground::Options(false, false, true));
        const int paper = brightness(bitmap.pixel(55, 55));
        const int minor = brightness(bitmap.pixel(60, 55));
        const int major = brightness(bitmap.pixel(50, 55));
        QVERIFY(minor > paper);
        QVERIFY(major > minor);
        QCOMPARE(brightness(bitmap.pixel(150, 155)), major); // every 50 pt
    }

    void textureIsDeterministicAndFaint()
    {
        QCOMPARE(PaperTexture::tile().width(), PaperTexture::pixels);
        QCOMPARE(PaperTexture::coverage(7, 64), PaperTexture::coverage(7, 64));
        QVERIFY(PaperTexture::coverage(7, 64) != PaperTexture::coverage(8, 64));
        QCOMPARE(PaperTexture::coverage(7, 64).size(), 64 * 64);

        const Bitmap bitmap = render(BlueprintBackground::Options(false, true, false));
        const QColor background = BlueprintPalette::blueprint().pageBackground;
        const int total = int(size.width() * size.height());
        QVERIFY2(bitmap.count(background, 1) < total, "some texture");
        QCOMPARE(bitmap.count(background, 40), total); // but faint
    }

    void titleBlockSitsInTheBottomRightCorner()
    {
        const QSizeF sheet(400, 300);
        Bitmap bitmap(int(sheet.width()), int(sheet.height()));
        BlueprintBackground::TitleBlock block;
        block.title = QStringLiteral("Survey");
        block.page = 1;
        block.pageCount = 3;
        block.date = QDateTime::currentDateTime();
        BlueprintBackground::Options options(false, false, false);
        options.titleBlock = block;
        {
            QPainter painter(&bitmap.image);
            BlueprintBackground::draw(painter, QRectF(QPointF(0, 0), sheet), BlueprintPalette::blueprint(), options);
        }
        const QSizeF tb = BlueprintBackground::titleBlockSize;
        const QRectF blockRect = QRectF(sheet.width() - BlueprintBackground::frameInset - tb.width(),
                                        sheet.height() - BlueprintBackground::frameInset - tb.height(), tb.width(), tb.height())
                                     .adjusted(2, 2, -2, -2);
        int ink = 0;
        for (int y = int(blockRect.top()); y < int(blockRect.bottom()); ++y)
            for (int x = int(blockRect.left()); x < int(blockRect.right()); ++x)
                if (bitmap.pixelMatches(x, y, Qt::white, 60))
                    ++ink;
        QVERIFY(ink > 100);
        QVERIFY(bitmap.pixelMatches(int(blockRect.left()) - 20, int(blockRect.center().y()),
                                    BlueprintPalette::blueprint().pageBackground, 2));
    }
};

QTEST_MAIN(BlueprintBackgroundTests)
#include "BlueprintBackgroundTests.moc"
