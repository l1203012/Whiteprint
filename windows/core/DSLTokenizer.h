#pragma once
#include "core/DrawingScene.h"
#include "core/Geometry.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <optional>

namespace wp {

/// A link between two ids in a chain like `a>b<>c-d`.
struct DSLLink {
    bool startArrow = false;
    bool endArrow = false;

    DSLLink() = default;
    DSLLink(bool startArrow, bool endArrow) : startArrow(startArrow), endArrow(endArrow) {}
    bool operator==(const DSLLink &) const = default;

    static DSLLink forward() { return DSLLink(false, true); }
};

/// `a>b>c`: ids joined by links. `links.size() == ids.size() - 1`.
struct DSLChain {
    QStringList ids;
    QList<DSLLink> links;
    bool operator==(const DSLChain &) const = default;
};

struct DSLToken {
    enum class Kind { word, string, point, size, number, chain };

    Kind kind = Kind::word;
    QString text;          // word, string
    GridPoint pointValue;  // point
    GridSize sizeValue;    // size
    double numberValue = 0; // number
    DSLChain chainValue;   // chain

    static DSLToken word(QString w) { DSLToken t; t.kind = Kind::word; t.text = std::move(w); return t; }
    static DSLToken string(QString s) { DSLToken t; t.kind = Kind::string; t.text = std::move(s); return t; }
    static DSLToken point(GridPoint p) { DSLToken t; t.kind = Kind::point; t.pointValue = p; return t; }
    static DSLToken size(GridSize s) { DSLToken t; t.kind = Kind::size; t.sizeValue = s; return t; }
    static DSLToken number(double n) { DSLToken t; t.kind = Kind::number; t.numberValue = n; return t; }
    static DSLToken chain(DSLChain c) { DSLToken t; t.kind = Kind::chain; t.chainValue = std::move(c); return t; }

    bool operator==(const DSLToken &) const = default;
};

/// Thrown by the tokenizer and parser for one bad line.
struct DSLSyntaxError {
    QString message;
    bool operator==(const DSLSyntaxError &) const = default;
};

namespace DSLTokenizer {

/// Largest coordinate or size accepted, in grid units.
inline constexpr double maxMagnitude = 1000.0;

/// Splits one line of drawing source into tokens. Throws DSLSyntaxError.
QList<DSLToken> tokenize(QStringView line);

/// Letters (any script), digits and `_`, not starting with a digit.
bool isIdentifier(QStringView raw);

} // namespace DSLTokenizer

} // namespace wp
