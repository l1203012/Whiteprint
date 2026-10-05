#pragma once
// Port of PDFExtractor.swift: reads a PDF one page at a time (QtPdf), falling back to OCR for pages
// without a text layer (scans, slides exported as images).
#include "extract/Extraction.h"

#include <QImage>

class QPdfDocument;

namespace wp::PDFExtractor {

/// Pages with fewer non-whitespace characters than this are OCR'd.
constexpr int ocrThreshold = 20;

/// The longest side of a rendered page, in pixels; keeps huge pages from allocating giant bitmaps.
constexpr double maxRenderSide = 4000;

/// One unit per page, ref `p. N`. Throws ExtractionError::unreadable.
std::vector<ExtractedUnit> extract(const QString &path, bool ocr);

int nonWhitespaceCount(const QString &text);

/// Renders page `index` at about 2x on white, honouring its rotation. Null image on failure.
QImage render(QPdfDocument &document, int index);

} // namespace wp::PDFExtractor
