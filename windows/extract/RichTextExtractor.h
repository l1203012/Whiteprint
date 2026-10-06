#pragma once
// Port of RichTextExtractor.swift: reads Word documents and splits them into sections at heading-like
// paragraphs.
//
// The Swift version relies on AppKit's importers; here .docx is read straight from the package (zip + XML),
// and .doc is handled as: RTF text (many ".doc" files are RTF), a .docx saved under the old name, or a real
// binary Word 97-2003 file (OLE container + piece table; text only, so headings are detected from their
// shape - short title-like line before a long paragraph - but not from bold/size).
#include "extract/Extraction.h"

namespace wp {

/// A paragraph as read from a source file, before heading detection.
struct RawParagraph {
    /// Whitespace-collapsed, non-empty.
    QString text;
    /// Every visible character is bold.
    bool bold = false;
    /// Largest font size in points (0 when unknown).
    double size = 0;
    /// Uses a paragraph style named "Heading N" or "Title" (docx).
    bool headingStyle = false;
};

namespace RichTextExtractor {

enum class Type { OfficeOpenXML, DocFormat };

/// Sections longer than this are split at paragraph boundaries.
constexpr int maxSectionLength = 6'000;
/// Headings in refs are cut to this many characters.
constexpr int maxHeadingLength = 60;

struct Paragraph {
    QString text;
    bool isHeading = false;
    friend bool operator==(const Paragraph &, const Paragraph &) = default;
};

/// Throws ExtractionError::unreadable.
std::vector<ExtractedUnit> extract(const QString &path, Type type);

/// Paragraphs with heading detection: bold or larger than the body font, or a short title-like line followed
/// by a long body paragraph.
std::vector<Paragraph> paragraphs(const std::vector<RawParagraph> &raw);

/// Groups paragraphs into sections that start at headings; stacked headings share a section. Long sections
/// are split at paragraphs.
std::vector<ExtractedUnit> sections(const std::vector<Paragraph> &paragraphs);

/// Raw paragraph readers, exposed for tests. They throw ExtractionError-free `std::runtime_error` on bad input.
std::vector<RawParagraph> docxParagraphs(const QByteArray &documentXml, const QByteArray &stylesXml);
std::vector<RawParagraph> rtfParagraphs(const QByteArray &rtf);
std::vector<RawParagraph> binaryDocParagraphs(const QByteArray &doc);

} // namespace RichTextExtractor

} // namespace wp
