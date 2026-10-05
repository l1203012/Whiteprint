#include "TestSupport.h"
#include "core/DSLParser.h"
#include "core/DSLTokenizer.h"

using namespace wp;
using namespace wp::testing;

class DSLParserTests : public QObject
{
    Q_OBJECT

private:
    static DSLStatement statement(const QString &line)
    {
        return DSLParser::statement(DSLTokenizer::tokenize(line));
    }

    static std::optional<QString> syntaxError(const QString &line)
    {
        try {
            statement(line);
            return std::nullopt;
        } catch (const DSLSyntaxError &e) {
            return e.message;
        }
    }

private slots:
    // MARK: Tokenizer

    void tokenizesEveryKind()
    {
        const auto tokens = DSLTokenizer::tokenize(
            QString::fromUtf8("box a -1.5,2 12x4 3 \"Say \\\"hi\\\"\\n\" a->b # comment"));
        const QList<DSLToken> expected = {
            DSLToken::word("box"),
            DSLToken::word("a"),
            DSLToken::point(GridPoint(-1.5, 2)),
            DSLToken::size(GridSize(12, 4)),
            DSLToken::number(3),
            DSLToken::string("Say \"hi\"\n"),
            DSLToken::chain({{"a", "b"}, {DSLLink::forward()}}),
        };
        WP_EQ(tokens, expected);
    }

    void hashInsideLabelIsNotAComment()
    {
        const auto tokens = DSLTokenizer::tokenize(QString::fromUtf8("text \"#1\""));
        WP_EQ(tokens, (QList<DSLToken>{DSLToken::word("text"), DSLToken::string("#1")}));
    }

    void linkOperators()
    {
        const auto tokens = DSLTokenizer::tokenize(QStringLiteral("a>b<c<>d-e->f<-g<->h--i"));
        const DSLChain chain{
            {"a", "b", "c", "d", "e", "f", "g", "h", "i"},
            {
                DSLLink(false, true),
                DSLLink(true, false),
                DSLLink(true, true),
                DSLLink(false, false),
                DSLLink(false, true),
                DSLLink(true, false),
                DSLLink(true, true),
                DSLLink(false, false),
            }};
        WP_EQ(tokens, (QList<DSLToken>{DSLToken::chain(chain)}));
    }

    void unicodeIdentifiers()
    {
        const auto tokens = DSLTokenizer::tokenize(U("flow Gebruiker>Dienst_\xc3\xa9\xc3\xa9n"));
        WP_EQ(tokens, (QList<DSLToken>{DSLToken::word("flow"),
                                       DSLToken::chain({{"Gebruiker", U("Dienst_\xc3\xa9\xc3\xa9n")},
                                                        {DSLLink::forward()}})}));
    }

    void tokenizerErrors_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("message");
        auto row = [](const char *line, const char *message) {
            QTest::newRow(line) << QString::fromUtf8(line) << QString::fromUtf8(message);
        };
        row("text \"open", "unterminated \"label\"");
        row("box a 1,2,3", "bad position '1,2,3', use X,Y");
        row("box a 1,", "bad position '1,', use X,Y");
        row("box a 4x", "bad size '4x', use WxH");
        row("box a 0x4", "size '0x4' must be positive");
        row("arrow a>", "bad link 'a>', use a>b");
        row("arrow a><b", "bad link 'a><b', use a>b");
        row("box a 5000,0", "'5000' is too large, max 1000");
        row("box a$", "unexpected 'a$'");
        row("box 1e5", "bad size '1e5', use WxH");
    }

    void tokenizerErrors()
    {
        QFETCH(QString, line);
        QFETCH(QString, message);
        const auto error = caught<DSLSyntaxError>([&] { DSLTokenizer::tokenize(line); });
        QVERIFY2(error.has_value(), qPrintable(line));
        QCOMPARE(error->message, message);
    }

    // MARK: Statements

    void shapeArgumentsInAnyOrder()
    {
        const auto expected = DSLStatement::shape(ShapeKind::box, QString("a"), GridPoint(1, 2), GridSize(12, 4),
                                                  QString("API"), DrawingStyle::dashed | DrawingStyle::thick);
        WP_EQ(statement(QStringLiteral("box a 1,2 12x4 \"API\" dashed thick")), expected);
        WP_EQ(statement(QStringLiteral("box thick \"API\" 12x4 a dashed 1,2")), expected);
    }

    void commandAliasesAndCase()
    {
        WP_EQ(statement("RECT a"),
              DSLStatement::shape(ShapeKind::box, QString("a"), std::nullopt, std::nullopt, std::nullopt,
                                  DrawingStyle()));
        WP_EQ(statement("oval o 6"),
              DSLStatement::shape(ShapeKind::circle, QString("o"), std::nullopt, GridSize(6, 6), std::nullopt,
                                  DrawingStyle()));
        WP_EQ(statement("column a b"),
              DSLStatement::layout(DSLLayout::col, DSLChain{{"a", "b"}, {DSLLink::forward()}}, std::nullopt));
    }

    void textIDIsOptional()
    {
        WP_EQ(statement("text 0,0 \"Hi\" bold"),
              DSLStatement::shape(ShapeKind::text, std::nullopt, GridPoint::zero(), std::nullopt, QString("Hi"),
                                  DrawingStyle::bold));
    }

    void connectors()
    {
        WP_EQ(statement("arrow a>b \"uses\""),
              DSLStatement::connect(DSLChain{{"a", "b"}, {DSLLink::forward()}}, QString("uses"), DrawingStyle()));
        WP_EQ(statement("arrow a b c"),
              DSLStatement::connect(DSLChain{{"a", "b", "c"}, {DSLLink::forward(), DSLLink::forward()}},
                                    std::nullopt, DrawingStyle()));
        WP_EQ(statement("line a b"),
              DSLStatement::connect(DSLChain{{"a", "b"}, {DSLLink(false, false)}}, std::nullopt, DrawingStyle()));
        WP_EQ(statement("arrow 0,0 4,0 dashed"),
              DSLStatement::polyline({GridPoint::zero(), GridPoint(4, 0)}, true, false, std::nullopt,
                                     DrawingStyle::dashed));
        WP_EQ(statement("path 0,0 4,0 4,4 closed"),
              DSLStatement::polyline({GridPoint::zero(), GridPoint(4, 0), GridPoint(4, 4)}, false, true,
                                     std::nullopt, DrawingStyle()));
    }

    void dimensionGroupAndLayouts()
    {
        WP_EQ(statement("dim 0,0 10,0 \"1 m\""),
              DSLStatement::dimension(GridPoint::zero(), GridPoint(10, 0), QString("1 m")));
        WP_EQ(statement("group g a b \"Backend\""),
              DSLStatement::group("g", {"a", "b"}, QString("Backend"), DrawingStyle()));
        WP_EQ(statement("row a b 2"),
              DSLStatement::layout(DSLLayout::row, DSLChain{{"a", "b"}, {DSLLink::forward()}}, 2.0));
        WP_EQ(statement("flow a<>b"),
              DSLStatement::layout(DSLLayout::flow, DSLChain{{"a", "b"}, {DSLLink(true, true)}}, std::nullopt));
    }

    void statementErrors_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("message");
        auto row = [](const char *line, const char *message) {
            QTest::newRow(line) << QString::fromUtf8(line) << QString::fromUtf8(message);
        };
        row("\"x\" box", "expected a command like box, arrow or flow");
        row("square a", "unknown command 'square'");
        row("box", "box needs an id, e.g. box a");
        row("box a b", "unexpected 'b'");
        row("box a 0,0 1,1", "only one position allowed");
        row("box a 4", "use WxH for the size, e.g. 12x4");
        row("circle c 4 4x4", "only one size allowed");
        row("box a \"x\" \"y\"", "only one \"label\" allowed");
        row("box a>b", "box can't link 'a>b', use arrow");
        row("box a closed", "'closed' only applies to path");
        row("text 0,0", "text needs a \"label\"");
        row("text \"a\" 4x4", "text has no size");
        row("arrow a", "use arrow a>b or arrow X,Y X,Y");
        row("arrow a>b c", "use arrow a>b or arrow X,Y X,Y");
        row("arrow a>b 3", "unexpected number '3'");
        row("path 0,0", "path needs at least 2 points, e.g. path 0,0 4,0 4,4 closed");
        row("dim 0,0", "dim needs 2 points, e.g. dim 0,0 10,0");
        row("group g", "group needs an id and members, e.g. group g a b");
        row("flow", "flow needs ids, e.g. flow a b c");
        row("row a b>c", "use either 'row a b c' or 'row a>b>c'");
        row("row a b -1", "gap can't be negative");
        row("row a b \"x\"", "row takes no \"label\"");
        row("col a b dashed", "col takes no style");
    }

    void statementErrors()
    {
        QFETCH(QString, line);
        QFETCH(QString, message);
        const auto error = syntaxError(line);
        QVERIFY2(error.has_value(), qPrintable(line));
        QCOMPARE(*error, message);
    }

    void parseReportsLineNumbersAndSkipsBlankAndCommentLines()
    {
        const auto result = DSLParser::parse(QStringLiteral("# title\n\nbox a\nsquare b\nbox c"));
        QList<int> numbers;
        for (const auto &l : result.lines)
            numbers.append(l.line);
        QCOMPARE(numbers, (QList<int>{3, 5}));
        WP_EQ(result.errors, (QList<DrawingError>{DrawingError(4, "unknown command 'square'")}));
        QCOMPARE(result.errors.first().description(), QString("line 4: unknown command 'square'"));
    }

    void statementLimit()
    {
        QStringList lines;
        for (int i = 0; i < DSLParser::maxStatements + 5; ++i)
            lines.append(QStringLiteral("box a"));
        const auto result = DSLParser::parse(lines.join(QLatin1Char('\n')));
        QCOMPARE(result.lines.size(), qsizetype(DSLParser::maxStatements));
        WP_EQ(result.errors,
              (QList<DrawingError>{DrawingError(DSLParser::maxStatements + 1, "too many lines, max 500")}));
    }
};

QTEST_APPLESS_MAIN(DSLParserTests)
#include "DSLParserTests.moc"
