#pragma once
#include <QFont>
#include <QString>
#include <QStringList>

namespace wp::fonts {

// The one place that decides which fonts stand in for the macOS system fonts.
// Sizes are pixel sizes: one unit per macOS point, on screen and (at 72 dpi) in PDF.

/// Stand-in for the macOS system font (San Francisco).
QStringList uiFamilies();
/// Stand-in for `monospacedSystemFont` (SF Mono).
QStringList monospaceFamilies();

QFont system(double size, QFont::Weight weight = QFont::Normal, bool italic = false);
QFont monospaced(double size, QFont::Weight weight = QFont::Normal);

/// True for fonts made by `monospaced`.
bool isMonospaced(const QFont &font);
/// The pixel size a font was made with.
double sizeOf(const QFont &font);
/// The same font at another size.
QFont withSize(const QFont &font, double size);

} // namespace wp::fonts
