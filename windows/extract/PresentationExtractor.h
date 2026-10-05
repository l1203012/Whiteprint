#pragma once
// Port of PresentationExtractor.swift: reads PowerPoint (.pptx) slides and speaker notes straight from the
// package, one slide at a time.
#include "extract/Extraction.h"
#include "extract/ZipArchive.h"

namespace wp {

namespace PresentationExtractor {

/// Throws ExtractionError::unreadable.
std::vector<ExtractedUnit> extract(const QString &path);

/// Slide part names in presentation order, from `ppt/presentation.xml`; falls back to `ppt/slides/slideN.xml`
/// in numeric order.
QStringList slidePaths(const ZipArchive &archive);

/// The slide's title first, then its other text, one line per paragraph, then the speaker notes as a
/// `Notes:` paragraph. Throws ZipArchive::Failure or XMLParseFailure.
QString slideText(const QString &path, const ZipArchive &archive);

} // namespace PresentationExtractor

/// Resolves relationship targets inside an Open Packaging Conventions package.
namespace PackagePath {

/// `../notesSlides/notesSlide1.xml` relative to `ppt/slides/slide1.xml` is `ppt/notesSlides/notesSlide1.xml`;
/// targets starting with `/` are relative to the package root.
QString resolve(const QString &target, const QString &source);

} // namespace PackagePath

} // namespace wp
