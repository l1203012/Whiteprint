#pragma once
// Port of TextCleaner.swift: normalizes raw extracted text so Claude reads content, not layout noise.
#include "extract/Extraction.h"

namespace wp::TextCleaner {

/// Cleans every unit, removes repeated headers and footers, and drops units left empty.
std::vector<ExtractedUnit> clean(const std::vector<ExtractedUnit> &units);

/// Cleans a single text: whitespace, hyphenation and page-number lines. Empty strings in the result mark
/// paragraph breaks.
QStringList cleanLines(const QString &text);

QString joinParagraphs(const QStringList &lines);

QString collapseWhitespace(const QString &text);

/// `12`, `- 12 -`, `Page 12`, `12 / 40`, `p. 3 of 9`, `Pagina 4 van 10`.
bool isPageNumber(const QString &line);

/// Removes lines that open or close more than half of the units (with at least three units): running
/// headers, course titles, footers. For a short first or last line digits are ignored, so
/// `Chapter 2 · 14` matches `Chapter 2 · 15`.
void removeRepeatedEdges(std::vector<QStringList> &units);

} // namespace wp::TextCleaner
