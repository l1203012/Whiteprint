#include "app/UiKit.h"

#include "app/MacStyle.h"
#include "render/Fonts.h"

#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace wp::ui {

void drawSymbol(QPainter &p, Symbol symbol, const QRectF &rect, const QColor &color)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const double s = qMin(rect.width(), rect.height());
    const QRectF r(rect.center().x() - s / 2, rect.center().y() - s / 2, s, s);
    // Unit coordinates 0...1 inside `r`.
    auto pt = [&](double x, double y) { return QPointF(r.left() + x * r.width(), r.top() + y * r.height()); };
    auto box = [&](double x, double y, double w, double h) {
        return QRectF(pt(x, y), QSizeF(w * r.width(), h * r.height()));
    };
    switch (symbol) {
    case Symbol::note:
    case Symbol::page:
    case Symbol::document: {
        QPainterPath path;
        path.moveTo(pt(0.22, 0.1));
        path.lineTo(pt(0.62, 0.1));
        path.lineTo(pt(0.82, 0.3));
        path.lineTo(pt(0.82, 0.9));
        path.lineTo(pt(0.22, 0.9));
        path.closeSubpath();
        p.drawPath(path);
        if (symbol != Symbol::document) {
            p.drawLine(pt(0.34, 0.5), pt(0.7, 0.5));
            p.drawLine(pt(0.34, 0.68), pt(0.7, 0.68));
        } else {
            p.drawLine(pt(0.34, 0.55), pt(0.7, 0.55));
            p.drawLine(pt(0.34, 0.72), pt(0.58, 0.72));
        }
        break;
    }
    case Symbol::command:
        p.drawLine(pt(0.3, 0.28), pt(0.55, 0.5));
        p.drawLine(pt(0.55, 0.5), pt(0.3, 0.72));
        p.drawLine(pt(0.6, 0.74), pt(0.8, 0.74));
        break;
    case Symbol::plus:
        p.drawLine(pt(0.5, 0.2), pt(0.5, 0.8));
        p.drawLine(pt(0.2, 0.5), pt(0.8, 0.5));
        break;
    case Symbol::deck:
        p.drawRoundedRect(box(0.2, 0.14, 0.6, 0.4), 2, 2);
        p.drawRoundedRect(box(0.12, 0.42, 0.6, 0.4), 2, 2);
        break;
    case Symbol::search:
        p.drawEllipse(box(0.14, 0.14, 0.5, 0.5));
        p.drawLine(pt(0.58, 0.58), pt(0.86, 0.86));
        break;
    case Symbol::folder: {
        QPainterPath path;
        path.moveTo(pt(0.1, 0.25));
        path.lineTo(pt(0.4, 0.25));
        path.lineTo(pt(0.5, 0.35));
        path.lineTo(pt(0.9, 0.35));
        path.lineTo(pt(0.9, 0.8));
        path.lineTo(pt(0.1, 0.8));
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Symbol::sparkles: {
        auto star = [&](double cx, double cy, double rad) {
            QPainterPath path;
            path.moveTo(pt(cx, cy - rad));
            path.quadTo(pt(cx, cy), pt(cx + rad, cy));
            path.quadTo(pt(cx, cy), pt(cx, cy + rad));
            path.quadTo(pt(cx, cy), pt(cx - rad, cy));
            path.quadTo(pt(cx, cy), pt(cx, cy - rad));
            p.drawPath(path);
        };
        star(0.42, 0.55, 0.32);
        star(0.78, 0.22, 0.13);
        break;
    }
    case Symbol::tray: {
        p.drawLine(pt(0.5, 0.12), pt(0.5, 0.56));
        p.drawLine(pt(0.34, 0.42), pt(0.5, 0.58));
        p.drawLine(pt(0.66, 0.42), pt(0.5, 0.58));
        QPainterPath path;
        path.moveTo(pt(0.12, 0.58));
        path.lineTo(pt(0.12, 0.84));
        path.lineTo(pt(0.88, 0.84));
        path.lineTo(pt(0.88, 0.58));
        p.drawPath(path);
        break;
    }
    case Symbol::xmark:
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(r.adjusted(1, 1, -1, -1));
        p.setPen(QPen(mac::isDark() ? QColor("#1e1e1e") : Qt::white, 1.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(pt(0.36, 0.36), pt(0.64, 0.64));
        p.drawLine(pt(0.64, 0.36), pt(0.36, 0.64));
        break;
    case Symbol::lock:
        p.drawRoundedRect(box(0.24, 0.46, 0.52, 0.38), 2, 2);
        p.drawArc(box(0.34, 0.14, 0.32, 0.5), 0, 180 * 16);
        break;
    case Symbol::hourglass: {
        QPainterPath path;
        path.moveTo(pt(0.28, 0.14));
        path.lineTo(pt(0.72, 0.14));
        path.lineTo(pt(0.28, 0.86));
        path.lineTo(pt(0.72, 0.86));
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Symbol::gear: {
        p.drawEllipse(box(0.3, 0.3, 0.4, 0.4));
        for (int i = 0; i < 8; ++i) {
            p.save();
            p.translate(r.center());
            p.rotate(i * 45);
            p.drawLine(QPointF(0, -r.height() * 0.28), QPointF(0, -r.height() * 0.42));
            p.restore();
        }
        break;
    }
    case Symbol::graduation: {
        QPainterPath path;
        path.moveTo(pt(0.5, 0.2));
        path.lineTo(pt(0.92, 0.42));
        path.lineTo(pt(0.5, 0.64));
        path.lineTo(pt(0.08, 0.42));
        path.closeSubpath();
        p.drawPath(path);
        p.drawLine(pt(0.26, 0.55), pt(0.26, 0.74));
        p.drawLine(pt(0.74, 0.55), pt(0.74, 0.74));
        p.drawLine(pt(0.26, 0.74), pt(0.5, 0.82));
        p.drawLine(pt(0.74, 0.74), pt(0.5, 0.82));
        break;
    }
    case Symbol::palette: {
        // A painter's palette: a round board with a thumb hole and three paint dabs.
        p.drawEllipse(box(0.12, 0.14, 0.76, 0.72));
        p.drawEllipse(box(0.56, 0.56, 0.14, 0.14));
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (const QPointF &dab : {pt(0.34, 0.36), pt(0.54, 0.3), pt(0.3, 0.58)})
            p.drawEllipse(dab, r.width() * 0.06, r.width() * 0.06);
        break;
    }
    }
    p.restore();
}

QIcon icon(Symbol symbol, const QColor &color, int size)
{
    QIcon result;
    for (const int scale : {1, 2}) {
        QPixmap pixmap(size * scale, size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        drawSymbol(painter, symbol, QRectF(0, 0, size, size), color);
        painter.end();
        result.addPixmap(pixmap);
    }
    return result;
}

SymbolWidget::SymbolWidget(Symbol symbol, int size, QWidget *parent) : QWidget(parent), m_symbol(symbol)
{
    setFixedSize(size, size);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void SymbolWidget::setSymbol(Symbol symbol)
{
    m_symbol = symbol;
    update();
}

void SymbolWidget::setColor(const QColor &color)
{
    m_color = color;
    m_custom = true;
    update();
}

void SymbolWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    drawSymbol(painter, m_symbol, rect(), m_custom ? m_color : mac::colors().secondaryText);
}

QLabel *label(const QString &text, double size, int weight, bool secondary, bool wrap, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setFont(fonts::system(size, QFont::Weight(weight)));
    l->setWordWrap(wrap);
    if (secondary)
        l->setForegroundRole(QPalette::PlaceholderText);
    return l;
}

} // namespace wp::ui
