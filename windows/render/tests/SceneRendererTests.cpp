#include "Bitmap.h"
#include "core/DrawingCompiler.h"
#include "render/DrawingView.h"
#include "render/ScenePainter.h"
#include "render/SceneRenderer.h"

#include <QtTest>
#include <cmath>

using namespace wp;
using namespace wp::testing;

class SceneRendererTests : public QObject
{
    Q_OBJECT

private:
    BlueprintPalette palette = BlueprintPalette::blueprint();

    static DrawingScene scene(const QString &source)
    {
        const CompiledDrawing compiled = DrawingCompiler::compile(source);
        if (!compiled.errors.isEmpty())
            QTest::qFail(qPrintable(QStringLiteral("compile errors in '%1'").arg(source)), __FILE__, __LINE__);
        return compiled.scene;
    }

    /// Renders `source` on the page background, canvas-sized.
    Bitmap render(const QString &source) const
    {
        const DrawingScene s = scene(source);
        const QSizeF size = SceneRenderer::canvasSize(s);
        Bitmap bitmap(int(size.width()), int(size.height()), palette.pageBackground);
        QPainter painter(&bitmap.image);
        SceneRenderer::draw(s, painter, palette);
        painter.end();
        return bitmap;
    }

    static bool near(QSizeF a, QSizeF b) { return std::abs(a.width() - b.width()) < 1e-6 && std::abs(a.height() - b.height()) < 1e-6; }

private slots:
    // MARK: Canvas size

    void emptySceneIsJustTheMargins()
    {
        QVERIFY(near(SceneRenderer::canvasSize(DrawingScene()), QSizeF(20, 20)));
    }

    void canvasIsBoundsInPointsPlusMargins()
    {
        QVERIFY(near(SceneRenderer::canvasSize(scene("box a 0,0 10x4")), QSizeF(120, 60)));
    }

    void canvasIgnoresWhereTheBoundsStart()
    {
        QVERIFY(near(SceneRenderer::canvasSize(scene("box a 5,7 10x4")), QSizeF(120, 60)));
    }

    void canvasIncludesGroupFrames()
    {
        // 10x4 box grown by 1.5 units of group padding on every side.
        QVERIFY(near(SceneRenderer::canvasSize(scene("box a 0,0 10x4\ngroup g a")), QSizeF(150, 90)));
    }

    // MARK: Pixels

    void boxStrokeIsTextColourAndInsideIsBackground()
    {
        const Bitmap bitmap = render("box a 0,0 10x4");
        QVERIFY2(bitmap.pixelMatches(10, 30, palette.text, 60), "left edge");
        QVERIFY2(bitmap.pixelMatches(60, 10, palette.text, 60), "top edge");
        QVERIFY(bitmap.pixelMatches(30, 25, palette.pageBackground));
        QVERIFY2(bitmap.pixelMatches(3, 3, palette.pageBackground), "margin");
    }

    void offsetSceneStartsAtTheMargin()
    {
        const Bitmap bitmap = render("box a 5,7 10x4");
        QVERIFY(bitmap.pixelMatches(10, 30, palette.text, 60));
    }

    void circleAndCylinder()
    {
        const Bitmap circle = render("circle c 0,0 4");
        QVERIFY(circle.pixelMatches(10, 30, palette.text, 60));
        QVERIFY(circle.pixelMatches(30, 30, palette.pageBackground));
        QVERIFY2(circle.pixelMatches(13, 13, palette.pageBackground), "corner outside the ellipse");

        const Bitmap db = render("db d 0,0 8x5");
        QVERIFY2(db.pixelMatches(50, 10, palette.text, 60), "top of the cap");
        QVERIFY2(db.pixelMatches(10, 40, palette.text, 60), "side");
        QVERIFY2(db.pixelMatches(50, 60, palette.text, 60), "bottom arc");
        QVERIFY2(db.pixelMatches(50, 40, palette.pageBackground), "body");
    }

    void arrowheadIsFilled()
    {
        const Bitmap arrow = render("arrow 0,0 10,0");
        const Bitmap line = render("line 0,0 10,0");
        QVERIFY2(arrow.pixel(104, 11).r > 0xC0, "inside the head");
        QVERIFY(line.pixelMatches(104, 12, palette.pageBackground));
        QVERIFY(line.pixelMatches(104, 10, palette.text, 60));
    }

    void dashedLineHasGaps()
    {
        const Bitmap dashed = render("line 0,0 20,0 dashed");
        const Bitmap solid = render("line 0,0 20,0");
        auto gaps = [&](const Bitmap &bitmap) {
            int n = 0;
            for (int x = 12; x < 208; ++x)
                if (bitmap.pixelMatches(x, 10, palette.pageBackground, 30))
                    ++n;
            return n;
        };
        QVERIFY(gaps(dashed) > 40);
        QCOMPARE(gaps(solid), 0);
    }

    void thickLinesAreWider()
    {
        // Total ink across a column, so partial coverage counts too.
        auto width = [&](const QString &source) {
            const Bitmap bitmap = render(source);
            int total = 0;
            for (int y = 0; y < bitmap.height(); ++y)
                total += bitmap.pixel(60, y).r;
            return total;
        };
        QVERIFY(width("line 0,0 10,0 thick") > width("line 0,0 10,0"));
    }

    void lineLabelKnocksOutTheLine()
    {
        const Bitmap bitmap = render(QStringLiteral("line 0,0 20,0 \" \""));
        QVERIFY(bitmap.pixelMatches(110, 10, palette.pageBackground));
        QVERIFY(bitmap.pixelMatches(40, 10, palette.text, 60));
    }

    void dimensionDrawsOffsetLineAndExtensionLines()
    {
        // Measured from (0,4) to (10,4); the offset line is 1.5 units above.
        const Bitmap bitmap = render("dim 0,4 10,4");
        QVERIFY2(std::max(bitmap.pixel(30, 9).r, bitmap.pixel(30, 10).r) > 0x70, "offset line");
        QVERIFY2(bitmap.pixelMatches(10, 20, palette.text, 120), "extension line");
        QVERIFY2(bitmap.pixelMatches(30, 25, palette.pageBackground), "nothing on the measured segment");
    }

    void groupFrameUsesMutedColour()
    {
        const Bitmap bitmap = render("box a 0,0 10x4\ngroup g a");
        bool muted = false;
        for (int y = 0; y < bitmap.height(); ++y) {
            const Pixel p = bitmap.pixel(10, y);
            muted = muted || (p.r > 0x60 && p.r < 0xF0);
        }
        QVERIFY2(muted, "muted stroke on the left edge");
    }

    void labelsDrawText()
    {
        const Bitmap blank = render("box a 0,0 10x4");
        const Bitmap labelled = render(QStringLiteral("box a 0,0 10x4 \"Label\""));
        QVERIFY(labelled.count(palette.text) > blank.count(palette.text) + 20);
    }

    // MARK: Geometry

    void midpointIsHalfwayAlongThePolyline()
    {
        const QPointF mid = ScenePainter::midpoint({QPointF(0, 0), QPointF(10, 0), QPointF(10, 30)});
        QVERIFY(std::abs(mid.x() - 10) < 0.001);
        QVERIFY(std::abs(mid.y() - 10) < 0.001);
    }

    void arrowShaftStopsUnderTheHead()
    {
        const ArrowHead head(false);
        const QPointF base = head.base(QPointF(100, 0), QPointF(0, 0));
        QVERIFY(std::abs(base.x() - (100 - head.length + 1)) < 0.001);
        QCOMPARE(head.base(QPointF(5, 0), QPointF(0, 0)), QPointF(5, 0)); // too short to shorten
    }

    // MARK: DrawingView

    void viewIntrinsicSizeIsTheCanvas()
    {
        DrawingView view(QStringLiteral("box a 0,0 10x4"));
        QCOMPARE(view.sizeHint(), QSize(120, 60));
    }

    void viewEmptyDrawingKeepsAClickableHeight()
    {
        DrawingView view{QString()};
        QCOMPARE(view.sizeHint().height(), int(DrawingView::minimumHeight));
    }

    void viewRecompilesWhenSourceChanges()
    {
        DrawingView view(QStringLiteral("arrow a>b"));
        QCOMPARE(view.errors().size(), 1);
        QCOMPARE(view.errors().first().line, 1);
        view.setSource(QStringLiteral("flow a>b"));
        QVERIFY(view.errors().isEmpty());
        QCOMPARE(view.scene().shapes.size(), 2);
        QCOMPARE(view.sizeHint().width(), int(std::ceil(SceneRenderer::canvasSize(view.scene()).width())));
    }

    void viewDrawsOnATransparentBackground()
    {
        DrawingView view(QStringLiteral("box a 0,0 10x4"));
        view.resize(view.sizeHint());
        QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        view.render(&image);
        QVERIFY(qAlpha(image.pixel(10, 30)) > 127);
        QCOMPARE(qAlpha(image.pixel(30, 25)), 0);
    }
};

QTEST_MAIN(SceneRendererTests)
#include "SceneRendererTests.moc"
