#pragma once
// Port of PresentationXML.swift: small QXmlStreamReader based parsers for PresentationML parts.
#include <QByteArray>
#include <QSet>
#include <QString>
#include <QStringList>
#include <exception>
#include <optional>
#include <vector>

namespace wp {

struct XMLParseFailure : std::exception {
    const char *what() const noexcept override { return "malformed XML"; }
};

/// The text of one shape on a slide or notes page.
struct ShapeText {
    /// The placeholder type (`title`, `body`, `sldNum`, ...), or nullopt for a free shape.
    std::optional<QString> placeholder;
    /// One entry per non-empty `<a:p>`.
    QStringList paragraphs;
    friend bool operator==(const ShapeText &, const ShapeText &) = default;
};

/// Collects `<a:t>` runs per `<a:p>` paragraph, grouped by shape.
struct ShapeTextParser {
    /// Shapes in document order, leaving out placeholders whose type is in `skipped`. Throws XMLParseFailure.
    static std::vector<ShapeText> parse(const QByteArray &data, const QSet<QString> &skipping = {});
};

/// One `<Relationship>` from a `.rels` part.
struct Relationship {
    QString id;
    QString type;
    QString target;
    friend bool operator==(const Relationship &, const Relationship &) = default;
};

struct RelationshipParser {
    /// Internal relationships; external links are left out. Malformed parts yield what was read before the error.
    static std::vector<Relationship> parse(const QByteArray &data);
};

/// Reads the relationship ids of `<p:sldId>` entries, in presentation order.
struct SlideListParser {
    static QStringList parse(const QByteArray &data);
};

} // namespace wp
