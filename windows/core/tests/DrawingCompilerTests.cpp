#include "TestSupport.h"
#include "core/DrawingCompiler.h"

#include <cmath>

using namespace wp;
using namespace wp::testing;

class DrawingCompilerTests : public QObject
{
    Q_OBJECT

private:
    static DrawingScene compile(const QString &source)
    {
        const CompiledDrawing result = DrawingCompiler::compile(source);
        if (!result.errors.isEmpty()) {
            QStringList d;
            for (const auto &e : result.errors)
                d.append(e.description());
            qWarning("unexpected errors: %s", qPrintable(d.join(QLatin1String("; "))));
        }
        if (!result.errors.isEmpty())
            QTest::qFail("compile produced errors", __FILE__, __LINE__);
        return result.scene;
    }

    static bool closeTo(double a, double b) { return std::abs(a - b) < 0.0001; }

    static bool isPoint(const std::optional<GridPoint> &p, double x, double y)
    {
        return p && closeTo(p->x, x) && closeTo(p->y, y);
    }

    static QList<GridPoint> origins(const DrawingScene &scene)
    {
        QList<GridPoint> r;
        for (const auto &s : scene.shapes)
            r.append(s.frame.origin);
        return r;
    }

    static std::optional<GridSize> sizeOf(const DrawingScene &scene, const char *id)
    {
        const SceneShape *s = scene.shape(id);
        return s ? std::optional<GridSize>(s->frame.size) : std::nullopt;
    }

    static std::optional<GridPoint> originOf(const DrawingScene &scene, const char *id)
    {
        const SceneShape *s = scene.shape(id);
        return s ? std::optional<GridPoint>(s->frame.origin) : std::nullopt;
    }

private slots:
    void emptySource()
    {
        const auto result = DrawingCompiler::compile(QString());
        WP_EQ(result.scene, DrawingScene());
        QVERIFY(result.errors.isEmpty());
        WP_EQ(result.scene.bounds(), GridRect::zero());
    }

    void explicitShapesKeepPositionAndSize()
    {
        const auto scene = compile(QStringLiteral("box a 2,3 12x6 \"API\" thick"));
        SceneShape expected;
        expected.id = "a";
        expected.kind = ShapeKind::box;
        expected.frame = GridRect(2, 3, 12, 6);
        expected.label = QString("API");
        expected.style = DrawingStyle::thick;
        WP_EQ(scene.shapes, (QList<SceneShape>{expected}));
    }

    void defaultSizesGrowToFitLabels()
    {
        const auto scene = compile(QStringLiteral(
            "box a 0,0\n"
            "box b 0,10 \"Authentication Service\"\n"
            "box c 0,20 \"two\\nlines\\nhere\"\n"
            "circle d 0,30\n"
            "db e 0,40\n"
            "text f 0,50 \"Hello\"\n"));
        WP_EQ(sizeOf(scene, "a"), std::optional<GridSize>(GridSize(10, 4)));
        WP_EQ(sizeOf(scene, "b"), std::optional<GridSize>(GridSize(20, 4))); // 22 chars x 0.8 -> 18, + 2 padding
        WP_EQ(sizeOf(scene, "c"), std::optional<GridSize>(GridSize(10, 8)));
        WP_EQ(sizeOf(scene, "d"), std::optional<GridSize>(GridSize(4, 4)));
        WP_EQ(sizeOf(scene, "e"), std::optional<GridSize>(GridSize(8, 5)));
        WP_EQ(sizeOf(scene, "f"), std::optional<GridSize>(GridSize(4, 2)));
    }

    void flowCreatesBoxesPlacesThemInARowAndLinksThem()
    {
        const auto scene = compile(QStringLiteral("flow Client>API>Web_App"));
        QStringList ids;
        for (const auto &s : scene.shapes)
            ids.append(s.id);
        QCOMPARE(ids, (QStringList{"Client", "API", "Web_App"}));
        WP_EQ(scene.shape("Web_App")->label, std::optional<QString>(QString("Web App")));
        WP_EQ(origins(scene), (QList<GridPoint>{GridPoint(0, 0), GridPoint(14, 0), GridPoint(28, 0)}));
        QCOMPARE(scene.lines.size(), qsizetype(2));
        WP_EQ(scene.lines[0].from, std::optional<QString>(QString("Client")));
        WP_EQ(scene.lines[0].to, std::optional<QString>(QString("API")));
        QVERIFY(isPoint(scene.lines[0].points.first(), 10, 2));
        QVERIFY(isPoint(scene.lines[0].points.last(), 14, 2));
        QVERIFY(scene.lines[0].endArrow);
        QVERIFY(!scene.lines[0].startArrow);
    }

    void unlabelledShapesShowTheirIDUnlessItIsAHandle()
    {
        const auto scene = compile(QStringLiteral("db Postgres\nbox Web_App\ncircle a\nbox b2\nbox DB\n"));
        QList<std::optional<QString>> labels;
        for (const auto &s : scene.shapes)
            labels.append(s.label);
        WP_EQ(labels, (QList<std::optional<QString>>{QString("Postgres"), QString("Web App"), std::nullopt,
                                                     std::nullopt, QString("DB")}));
        WP_EQ(sizeOf(scene, "Postgres"), std::optional<GridSize>(GridSize(9, 5)));
    }

    void flowUsesDeclaredShapesEvenWhenDeclaredLater()
    {
        const auto scene = compile(QStringLiteral("flow a>b\ndb b \"Postgres\"\n"));
        QVERIFY(scene.shape("b")->kind == ShapeKind::db);
        WP_EQ(scene.shape("b")->frame, GridRect(14, -0.5, 9, 5));
    }

    void rowCentersVerticallyAndColCentersHorizontally()
    {
        const auto scene = compile(QStringLiteral("box a 0,0\ncircle b\ncol a c 2\nrow a b\n"));
        WP_EQ(originOf(scene, "b"), std::optional<GridPoint>(GridPoint(14, 0)));
        WP_EQ(originOf(scene, "c"), std::optional<GridPoint>(GridPoint(0, 6)));
        QVERIFY(scene.lines.isEmpty());
    }

    void explicitPositionWinsOverLayout()
    {
        const auto scene = compile(QStringLiteral("box b 50,50\nrow a b\n"));
        WP_EQ(originOf(scene, "b"), std::optional<GridPoint>(GridPoint(50, 50)));
    }

    void secondLayoutStartsBelowTheFirst()
    {
        const auto scene = compile(QStringLiteral("flow a>b\nflow c>d\n"));
        WP_EQ(originOf(scene, "c"), std::optional<GridPoint>(GridPoint(0, 8)));
    }

    void unplacedShapesGoInARowBelowEverything()
    {
        const auto scene = compile(QStringLiteral("box a 0,0\nbox b\ncircle c\n"));
        WP_EQ(originOf(scene, "b"), std::optional<GridPoint>(GridPoint(0, 8)));
        WP_EQ(originOf(scene, "c"), std::optional<GridPoint>(GridPoint(14, 8)));
    }

    void unnamedTextGetsInternalIDs()
    {
        const auto scene = compile(QStringLiteral("text \"one\"\ntext _t1 \"named\"\ntext \"two\"\n"));
        QStringList ids;
        for (const auto &s : scene.shapes)
            ids.append(s.id);
        QCOMPARE(ids, (QStringList{"_t2", "_t1", "_t3"}));
    }

    void arrowsClipToRectangleAndEllipseEdges()
    {
        const auto scene = compile(QStringLiteral(
            "box a 0,0 10x4\n"
            "circle b 20,0 4\n"
            "arrow a>b\n"
            "line a<>b \"x\" dashed\n"));
        QVERIFY(isPoint(scene.lines[0].points[0], 10, 2));
        QVERIFY(isPoint(scene.lines[0].points[1], 20, 2));
        WP_EQ(scene.lines[1].label, std::optional<QString>(QString("x")));
        WP_EQ(scene.lines[1].style, DrawingStyle::dashed);
        QVERIFY(scene.lines[1].startArrow && scene.lines[1].endArrow);
    }

    void diagonalArrowLeavesThroughTheCorrectSide()
    {
        const auto scene = compile(QStringLiteral("box a 0,0 10x4\nbox b 0,20 10x4\narrow a>b\n"));
        QVERIFY(isPoint(scene.lines[0].points[0], 5, 4));
        QVERIFY(isPoint(scene.lines[0].points[1], 5, 20));
    }

    void groupsWrapMembersAndCanBeLinked()
    {
        const auto scene = compile(QStringLiteral(
            "box a 0,0\n"
            "box b 14,0\n"
            "box c 0,20\n"
            "group g a b \"Backend\"\n"
            "arrow c>g\n"));
        const SceneGroup *group = scene.group("g");
        QVERIFY(group);
        WP_EQ(group->frame, GridRect(-1.5, -3.5, 27, 9));
        WP_EQ(group->style, DrawingStyle::dashed);
        WP_EQ(scene.lines.first().to, std::optional<QString>(QString("g")));
        QVERIFY(isPoint(scene.lines.first().points.last(), 10.5, 5.5));
    }

    void dimensionsDefaultToTheirLength()
    {
        const auto scene = compile(QStringLiteral("dim 0,0 12,0\ndim 0,0 3,4\ndim 0,0 1,1\ndim 0,0 0,5 \"5 m\"\n"));
        QStringList labels;
        for (const auto &d : scene.dimensions)
            labels.append(d.label);
        QCOMPARE(labels, (QStringList{"12", "5", "1.4", "5 m"}));
        const auto offset = scene.dimensions[0].offsetLine();
        QVERIFY(isPoint(offset.first, 0, -1.5));
        QVERIFY(isPoint(offset.second, 12, -1.5));
    }

    void boundsCoverShapesLinesGroupsAndDimensions()
    {
        const auto scene = compile(QStringLiteral("box a 0,0\nline -5,2 3,30\ndim 0,0 10,0\n"));
        WP_EQ(scene.bounds(), GridRect(-5, -1.5, 15, 31.5));
    }

    void errorsAreReportedButTheRestStillDraws()
    {
        const auto result = DrawingCompiler::compile(QStringLiteral(
            "box a\n"
            "box a\n"
            "arrow a>x\n"
            "arrow a>a\n"
            "group g a nope\n"
            "row g a\n"
            "box g\n"
            "squiggle\n"));
        QStringList descriptions;
        for (const auto &e : result.errors)
            descriptions.append(e.description());
        QCOMPARE(descriptions,
                 (QStringList{
                     "line 2: 'a' is already defined on line 1",
                     "line 3: unknown id 'x'",
                     "line 4: can't link 'a' to itself",
                     "line 5: 'g' is already defined on line 7",
                     "line 6: can't lay out group 'g'",
                     "line 8: unknown command 'squiggle'",
                 }));
        QStringList ids;
        for (const auto &s : result.scene.shapes)
            ids.append(s.id);
        QCOMPARE(ids, (QStringList{"a", "g"}));
        QVERIFY(result.scene.lines.isEmpty());
    }

    void threeTierExampleFromThePlan()
    {
        const auto scene = compile(QStringLiteral(
            "box c 0,0 \"Client\"\n"
            "box s 12,0 \"Server\"\n"
            "db  d 24,0 \"Postgres\"\n"
            "arrow c>s \"HTTPS\"\n"
            "arrow s>d\n"));
        QCOMPARE(scene.shapes.size(), qsizetype(3));
        QList<std::optional<QString>> labels;
        for (const auto &l : scene.lines)
            labels.append(l.label);
        WP_EQ(labels, (QList<std::optional<QString>>{QString("HTTPS"), std::nullopt}));
    }
};

QTEST_APPLESS_MAIN(DrawingCompilerTests)
#include "DrawingCompilerTests.moc"
