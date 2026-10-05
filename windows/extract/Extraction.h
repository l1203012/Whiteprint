#pragma once
// Port of Extraction.swift: the value types and entry points of the document extractor.
#include <QByteArray>
#include <QSet>
#include <QString>
#include <QStringList>
#include <exception>
#include <vector>

namespace wp {

/// One page, slide or section of a source document.
struct ExtractedUnit {
    /// Human-readable location, e.g. `p. 6`, `slide 14`, `section 3`.
    QString ref;
    QString text;
    friend bool operator==(const ExtractedUnit &, const ExtractedUnit &) = default;
};

struct ExtractedDocument {
    /// File name, e.g. `Lecture3.pptx`.
    QString name;
    /// Cleaned units in document order. Empty units are dropped.
    std::vector<ExtractedUnit> units;
    friend bool operator==(const ExtractedDocument &, const ExtractedDocument &) = default;
};

/// A slice of a document small enough to send to Claude in one tool result.
struct ExtractedChunk {
    /// 1-based.
    int index = 0;
    QString firstRef;
    QString lastRef;
    /// Units joined with `[ref]` marker lines, e.g. `[slide 14]`, so every line can be traced back to its source.
    QString text;
    friend bool operator==(const ExtractedChunk &, const ExtractedChunk &) = default;
};

/// Thrown by DocumentExtractor::extract.
class ExtractionError : public std::exception {
public:
    enum class Kind { UnsupportedType, Unreadable, Empty };

    ExtractionError(Kind kind, QString name);
    Kind kind() const { return m_kind; }
    /// The file name the error is about.
    const QString &name() const { return m_name; }
    /// "<name>: can't be read" etc., the same wording as the Swift app.
    QString description() const;
    const char *what() const noexcept override { return m_what.constData(); }

    static ExtractionError unsupportedType(const QString &name) { return {Kind::UnsupportedType, name}; }
    static ExtractionError unreadable(const QString &name) { return {Kind::Unreadable, name}; }
    static ExtractionError empty(const QString &name) { return {Kind::Empty, name}; }

    friend bool operator==(const ExtractionError &a, const ExtractionError &b) {
        return a.m_kind == b.m_kind && a.m_name == b.m_name;
    }

private:
    Kind m_kind;
    QString m_name;
    QByteArray m_what;
};

struct DocumentExtractor {
    /// "pdf", "docx", "doc", "pptx".
    static const QSet<QString> &supportedExtensions();

    /// Extracts and cleans text locally. Runs synchronously and may take a while for large or scanned
    /// files, so call it off the GUI thread. Throws ExtractionError.
    static ExtractedDocument extract(const QString &path, bool ocr = true);
};

struct Chunker {
    /// About 6k tokens.
    static constexpr int defaultMaxCharacters = 24'000;

    /// Splits at unit boundaries; a single oversized unit is split at paragraph or line boundaries.
    static std::vector<ExtractedChunk> chunks(const ExtractedDocument &document, int maxCharacters = defaultMaxCharacters);
};

} // namespace wp
