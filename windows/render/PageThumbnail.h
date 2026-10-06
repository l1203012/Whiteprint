#pragma once
#include "core/Note.h"
#include "render/Palette.h"

#include <QImage>
#include <QSize>
#include <QSizeF>
#include <QString>

class QPainter;

namespace wp {

namespace PageThumbnail {

/// Width of the page the thumbnail shows, in points; content is laid out at
/// this width and scaled down to the thumbnail.
inline constexpr double pageWidth = 480;
inline constexpr double padding = 32;
inline constexpr double fontSize = 15;
/// Text scaled below this many points is drawn as bars instead.
inline constexpr double minimumTextSize = 5;

/// A small preview of a page for the sidebar, `size` in device-independent
/// pixels (the image has `size * devicePixelRatio` pixels).
QImage image(const NotePage &page, QSize size, const BlueprintPalette &palette = BlueprintPalette::blueprint(),
             double devicePixelRatio = 1);

/// The line without its heading marker and inline markup.
QString plainText(const QString &line, int markerLength);

/// Draws the preview into a y-down `painter`.
void draw(const NotePage &page, QPainter &painter, QSizeF size, const BlueprintPalette &palette);

} // namespace PageThumbnail

} // namespace wp
