#pragma once
#include "render/MarkdownStyler.h"

#include <QChar>
#include <QRectF>

class QPainter;
class QTextDocument;

namespace wp {

/// Concealed Markdown in a text block (the counterpart of the macOS
/// `MarkdownLayoutManager`). The document text stays raw Markdown; the
/// styler marks the characters to draw differently with `markupProperty`.
///
/// Hidden markup (and rules) are made zero-width and transparent with
/// negative letter spacing, so they take no room but the caret and hit
/// testing still work on the real characters. Bullets and checkboxes keep
/// their space but their glyphs are transparent; `draw` paints a bullet,
/// a box or a rule over their place.
namespace ConcealedGlyphs {

/// Hidden markup and rules take no space.
bool isZeroWidth(MarkdownStyler::Markup markup);
QChar bulletCharacter();

/// Makes the concealed runs of `document` zero-width and transparent.
/// Run after every `MarkdownStyler::apply`. Idempotent.
void applyZeroWidth(QTextDocument *document);

/// The rectangle of the character at `index`, in document (= viewport) coordinates,
/// with the line's ascent in `ascent`. Empty if the layout isn't ready.
QRectF characterRect(const QTextDocument *document, int index, double *ascent = nullptr);

/// Paints bullets, checkboxes and rules of the blocks touching `exposed`.
void draw(QPainter &painter, const QTextDocument *document, const BlueprintPalette &palette, const QRectF &exposed);

} // namespace ConcealedGlyphs

} // namespace wp
