#include "core/DSLParser.h"

#include "core/TextUtil.h"

#include <QHash>

namespace wp {

DSLStatement DSLStatement::shape(ShapeKind kind, std::optional<QString> id, std::optional<GridPoint> at,
                                 std::optional<GridSize> size, std::optional<QString> label, DrawingStyle style)
{
    DSLStatement s;
    s.kind = Kind::shape;
    s.shapeKind = kind;
    s.id = std::move(id);
    s.at = at;
    s.size = size;
    s.label = std::move(label);
    s.style = style;
    return s;
}

DSLStatement DSLStatement::connect(DSLChain chain, std::optional<QString> label, DrawingStyle style)
{
    DSLStatement s;
    s.kind = Kind::connect;
    s.chain = std::move(chain);
    s.label = std::move(label);
    s.style = style;
    return s;
}

DSLStatement DSLStatement::polyline(QList<GridPoint> points, bool arrow, bool closed, std::optional<QString> label,
                                    DrawingStyle style)
{
    DSLStatement s;
    s.kind = Kind::polyline;
    s.points = std::move(points);
    s.arrow = arrow;
    s.closed = closed;
    s.label = std::move(label);
    s.style = style;
    return s;
}

DSLStatement DSLStatement::dimension(GridPoint from, GridPoint to, std::optional<QString> label)
{
    DSLStatement s;
    s.kind = Kind::dimension;
    s.from = from;
    s.to = to;
    s.label = std::move(label);
    return s;
}

DSLStatement DSLStatement::group(QString id, QStringList members, std::optional<QString> label, DrawingStyle style)
{
    DSLStatement s;
    s.kind = Kind::group;
    s.groupID = std::move(id);
    s.members = std::move(members);
    s.label = std::move(label);
    s.style = style;
    return s;
}

DSLStatement DSLStatement::layout(DSLLayout kind, DSLChain chain, std::optional<double> gap)
{
    DSLStatement s;
    s.kind = Kind::layout;
    s.layoutKind = kind;
    s.chain = std::move(chain);
    s.gap = gap;
    return s;
}

namespace DSLParser {
namespace {

[[noreturn]] void fail(const QString &message)
{
    throw DSLSyntaxError{message};
}

/// A statement's arguments sorted by kind. Order between kinds doesn't matter,
/// so `box a "API" 0,0` and `box a 0,0 "API"` mean the same thing.
struct Arguments {
    QStringList words;
    QStringList labels;
    QList<GridPoint> points;
    QList<GridSize> sizes;
    QList<double> numbers;
    QList<DSLChain> chains;
    DrawingStyle style;
    bool closed = false;

    explicit Arguments(const QList<DSLToken> &tokens)
    {
        for (qsizetype i = 1; i < tokens.size(); ++i) {
            const DSLToken &token = tokens[i];
            switch (token.kind) {
            case DSLToken::Kind::word:
                if (token.text == QLatin1String("dashed"))
                    style.insert(DrawingStyle::dashed);
                else if (token.text == QLatin1String("thick"))
                    style.insert(DrawingStyle::thick);
                else if (token.text == QLatin1String("bold"))
                    style.insert(DrawingStyle::bold);
                else if (token.text == QLatin1String("closed"))
                    closed = true;
                else
                    words.append(token.text);
                break;
            case DSLToken::Kind::string: labels.append(token.text); break;
            case DSLToken::Kind::point: points.append(token.pointValue); break;
            case DSLToken::Kind::size: sizes.append(token.sizeValue); break;
            case DSLToken::Kind::number: numbers.append(token.numberValue); break;
            case DSLToken::Kind::chain: chains.append(token.chainValue); break;
            }
        }
    }

    std::optional<QString> label() const
    {
        if (labels.size() > 1)
            fail(QStringLiteral("only one \"label\" allowed"));
        if (labels.isEmpty())
            return std::nullopt;
        return labels.first();
    }

    struct Rejects {
        bool chains = false, points = false, sizes = false, numbers = false, words = false, labels = false;
    };

    void reject(Rejects r, const QString &command) const
    {
        if (r.chains && !chains.isEmpty())
            fail(QStringLiteral("%1 can't link '%2', use arrow").arg(command, chains.first().ids.join(QLatin1Char('>'))));
        if (r.points && !points.isEmpty())
            fail(QStringLiteral("%1 takes no position").arg(command));
        if (r.sizes && !sizes.isEmpty())
            fail(QStringLiteral("%1 takes no size").arg(command));
        if (r.numbers && !numbers.isEmpty())
            fail(QStringLiteral("unexpected number '%1'").arg(formatDouble(numbers.first())));
        if (r.words && !words.isEmpty())
            fail(QStringLiteral("unexpected '%1'").arg(words.first()));
        if (r.labels && !labels.isEmpty())
            fail(QStringLiteral("%1 takes no \"label\"").arg(command));
    }

    void rejectClosed() const
    {
        if (closed)
            fail(QStringLiteral("'closed' only applies to path"));
    }
};

DSLStatement shape(ShapeKind kind, const Arguments &args)
{
    const QString command = shapeKindName(kind);
    args.reject({.chains = true}, command);
    args.rejectClosed();
    if (args.words.size() > 1)
        fail(QStringLiteral("unexpected '%1'").arg(args.words[1]));
    std::optional<QString> id;
    if (!args.words.isEmpty())
        id = args.words.first();
    const std::optional<QString> label = args.label();
    if (kind == ShapeKind::text) {
        if (!label)
            fail(QStringLiteral("text needs a \"label\""));
        if (!args.sizes.isEmpty() || !args.numbers.isEmpty())
            fail(QStringLiteral("text has no size"));
    } else if (!id) {
        fail(QStringLiteral("%1 needs an id, e.g. %1 a").arg(command));
    }
    if (args.points.size() > 1)
        fail(QStringLiteral("only one position allowed"));

    std::optional<GridSize> size;
    if (!args.sizes.isEmpty())
        size = args.sizes.first();
    if (!args.numbers.isEmpty()) {
        const double diameter = args.numbers.first();
        if (kind != ShapeKind::circle)
            fail(QStringLiteral("use WxH for the size, e.g. 12x4"));
        if (!(diameter > 0))
            fail(QStringLiteral("size must be positive"));
        size = GridSize(diameter, diameter);
    }
    if (args.sizes.size() + args.numbers.size() > 1)
        fail(QStringLiteral("only one size allowed"));
    std::optional<GridPoint> at;
    if (!args.points.isEmpty())
        at = args.points.first();
    return DSLStatement::shape(kind, id, at, size, label, args.style);
}

DSLStatement connector(bool arrow, const Arguments &args)
{
    const QString command = arrow ? QStringLiteral("arrow") : QStringLiteral("line");
    args.reject({.sizes = true, .numbers = true}, command);
    args.rejectClosed();
    const std::optional<QString> label = args.label();
    const QString usage = QStringLiteral("use %1 a>b or %1 X,Y X,Y").arg(command);

    if (args.chains.size() == 1 && args.points.isEmpty() && args.words.isEmpty())
        return DSLStatement::connect(args.chains[0], label, args.style);
    if (args.chains.isEmpty() && args.words.isEmpty() && args.points.size() >= 2)
        return DSLStatement::polyline(args.points, arrow, false, label, args.style);
    // `arrow a b` is forgiven and read as `arrow a>b`.
    if (args.chains.isEmpty() && args.points.isEmpty() && args.words.size() >= 2) {
        DSLChain chain;
        chain.ids = args.words;
        chain.links = QList<DSLLink>(args.words.size() - 1, DSLLink(false, arrow));
        return DSLStatement::connect(chain, label, args.style);
    }
    fail(usage);
}

DSLStatement layout(DSLLayout kind, const Arguments &args)
{
    const QString command = kind == DSLLayout::row ? QStringLiteral("row")
        : kind == DSLLayout::col                   ? QStringLiteral("col")
                                                   : QStringLiteral("flow");
    args.reject({.points = true, .sizes = true, .labels = true}, command);
    args.rejectClosed();
    if (args.numbers.size() > 1)
        fail(QStringLiteral("only one gap allowed"));
    if (!args.numbers.isEmpty() && args.numbers.first() < 0)
        fail(QStringLiteral("gap can't be negative"));
    if (!args.style.isEmpty())
        fail(QStringLiteral("%1 takes no style").arg(command));

    DSLChain chain;
    if (args.chains.size() == 1 && args.words.isEmpty()) {
        chain = args.chains[0];
    } else if (args.chains.isEmpty() && !args.words.isEmpty()) {
        chain.ids = args.words;
        chain.links = QList<DSLLink>(args.words.size() - 1, DSLLink::forward());
    } else if (args.chains.isEmpty() && args.words.isEmpty()) {
        fail(QStringLiteral("%1 needs ids, e.g. %1 a b c").arg(command));
    } else {
        fail(QStringLiteral("use either '%1 a b c' or '%1 a>b>c'").arg(command));
    }
    std::optional<double> gap;
    if (!args.numbers.isEmpty())
        gap = args.numbers.first();
    return DSLStatement::layout(kind, chain, gap);
}

QString canonicalCommand(const QString &word)
{
    static const QHash<QString, QString> commands = {
        {"box", "box"},     {"rect", "box"},  {"circle", "circle"}, {"oval", "circle"}, {"db", "db"},
        {"text", "text"},   {"arrow", "arrow"}, {"line", "line"},   {"path", "path"},   {"dim", "dim"},
        {"group", "group"}, {"row", "row"},   {"col", "col"},       {"column", "col"},  {"flow", "flow"},
    };
    return commands.value(word.toLower());
}

} // namespace

DSLStatement statement(const QList<DSLToken> &tokens)
{
    if (tokens.isEmpty() || tokens[0].kind != DSLToken::Kind::word)
        fail(QStringLiteral("expected a command like box, arrow or flow"));
    const QString word = tokens[0].text;
    const QString command = canonicalCommand(word);
    if (command.isEmpty())
        fail(QStringLiteral("unknown command '%1'").arg(word));
    const Arguments args(tokens);

    if (command == QLatin1String("box") || command == QLatin1String("circle") || command == QLatin1String("db")
        || command == QLatin1String("text"))
        return shape(*shapeKindFromName(command), args);
    if (command == QLatin1String("arrow") || command == QLatin1String("line"))
        return connector(command == QLatin1String("arrow"), args);
    if (command == QLatin1String("path")) {
        args.reject({.chains = true, .sizes = true, .numbers = true, .words = true}, command);
        if (args.points.size() < 2)
            fail(QStringLiteral("path needs at least 2 points, e.g. path 0,0 4,0 4,4 closed"));
        return DSLStatement::polyline(args.points, false, args.closed, args.label(), args.style);
    }
    if (command == QLatin1String("dim")) {
        args.reject({.chains = true, .sizes = true, .numbers = true, .words = true}, command);
        args.rejectClosed();
        if (args.points.size() != 2)
            fail(QStringLiteral("dim needs 2 points, e.g. dim 0,0 10,0"));
        return DSLStatement::dimension(args.points[0], args.points[1], args.label());
    }
    if (command == QLatin1String("group")) {
        args.reject({.chains = true, .points = true, .sizes = true, .numbers = true}, command);
        args.rejectClosed();
        if (args.words.size() < 2)
            fail(QStringLiteral("group needs an id and members, e.g. group g a b"));
        return DSLStatement::group(args.words[0], args.words.mid(1), args.label(), args.style);
    }
    return layout(command == QLatin1String("row") ? DSLLayout::row
                      : command == QLatin1String("col") ? DSLLayout::col
                                                        : DSLLayout::flow,
                  args);
}

DSLParseResult parse(const QString &source)
{
    DSLParseResult result;
    const QList<QStringView> sourceLines = QStringView(source).split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (qsizetype offset = 0; offset < sourceLines.size(); ++offset) {
        const int number = int(offset) + 1;
        try {
            const QList<DSLToken> tokens = DSLTokenizer::tokenize(sourceLines[offset]);
            if (tokens.isEmpty())
                continue;
            if (result.lines.size() >= maxStatements) {
                result.errors.append(DrawingError(number, QStringLiteral("too many lines, max %1").arg(maxStatements)));
                break;
            }
            DSLParsedLine parsed;
            parsed.line = number;
            parsed.statement = statement(tokens);
            result.lines.append(parsed);
        } catch (const DSLSyntaxError &error) {
            result.errors.append(DrawingError(number, error.message));
        }
    }
    return result;
}

} // namespace DSLParser
} // namespace wp
