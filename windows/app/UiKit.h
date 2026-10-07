#pragma once
// Small shared pieces for the palette, settings, flashcard and study windows: painted stand-ins for
// the SF Symbols the AppKit code uses, and a few styled-label helpers.
#include <QColor>
#include <QIcon>
#include <QRectF>
#include <QWidget>

class QLabel;
class QPainter;

namespace wp::ui {

enum class Symbol {
    note, page, command, deck, search, folder, sparkles, tray, document, xmark, lock, hourglass, gear, graduation, plus, palette
};

/// Draws `symbol` with a 1.4 px stroke in `color`, fitted into `rect`.
void drawSymbol(QPainter &painter, Symbol symbol, const QRectF &rect, const QColor &color);

QIcon icon(Symbol symbol, const QColor &color, int size = 18);

/// A fixed-size widget showing one symbol, tinted with the secondary text colour (or `color`).
class SymbolWidget : public QWidget {
public:
    explicit SymbolWidget(Symbol symbol, int size = 18, QWidget *parent = nullptr);
    void setSymbol(Symbol symbol);
    void setColor(const QColor &color);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Symbol m_symbol;
    QColor m_color;
    bool m_custom = false;
};

/// A label in the shared style: `secondary` uses the secondary text colour, `size` is the pixel size.
/// Colours are applied through the palette so they follow light/dark changes on re-polish.
QLabel *label(const QString &text, double size = 13, int weight = 400, bool secondary = false, bool wrap = false,
              QWidget *parent = nullptr);

} // namespace wp::ui
