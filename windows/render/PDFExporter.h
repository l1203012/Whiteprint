#pragma once
#include "core/Note.h"
#include "render/Palette.h"

#include <QByteArray>
#include <QDateTime>
#include <QSizeF>
#include <QStringList>
#include <optional>

namespace wp {

namespace PDFExporter {

inline constexpr double margin = 56;
inline constexpr double fontSize = 11;
inline const QSizeF a4{595.28, 841.89};
inline const QSizeF letter{612, 792};

/// A4 or US Letter by locale, typeset text only (drawings are left out),
/// one or more PDF pages per note page, page breaks at `+++page`.
/// Note pages without any text are skipped, except the first.
/// Blueprint pages carry a title block dated `date`.
QByteArray data(const Note &note, PDFExportStyle style, const QDateTime &date = QDateTime::currentDateTime());

/// US Letter in the US and Canada (two-letter region code), A4 elsewhere.
QSizeF paperSize(const std::optional<QString> &region);

/// The text of each note page with drawings left out. Text-less pages
/// after the first are dropped.
QStringList pageTexts(const Note &note);

} // namespace PDFExporter

} // namespace wp
