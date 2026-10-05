#include "editor/DeckBlockView.h"

#include "render/Fonts.h"

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace wp {

// MARK: DeckLayout

DeckLayout::Metrics DeckLayout::metrics(double fontSize)
{
    const double k = fontSize / MarkdownStyler::defaultFontSize;
    Metrics m;
    const double questionSize = std::round(fontSize * 0.95 * 2) / 2;
    m.padding = std::round(12 * k);
    m.headerHeight = std::round(30 * k);
    m.buttonHeight = std::round(22 * k);
    m.rowSpacing = std::round(6 * k);
    m.rowPadding = std::round(12 * k);
    m.rowVerticalPadding = std::round(8 * k);
    m.answerGap = std::round(3 * k);
    m.hintWidth = std::round(48 * k);
    m.questionSize = questionSize;
    m.answerSize = std::round(fontSize * 0.9 * 2) / 2;
    m.refSize = std::round(fontSize * 0.75 * 2) / 2;
    m.questionLineHeight = std::ceil(QFontMetricsF(fonts::system(questionSize)).lineSpacing());
    return m;
}

DeckLayout::DeckLayout(const CardDeck &deck, double width, double fontSize, const QSet<int> &revealed) : width(width)
{
    const Metrics m = metrics(fontSize);
    header = QRectF(m.padding, m.padding, std::max(0.0, width - 2 * m.padding), m.headerHeight);
    double y = header.bottom() + m.rowSpacing;
    const BlueprintPalette palette = BlueprintPalette::blueprint();
    for (int index = 0; index < deck.cards.size(); ++index) {
        const bool open = revealed.contains(index);
        const double textWidth = std::max(20.0, header.width() - 2 * m.rowPadding - (open ? 0 : m.hintWidth));
        const QList<DeckText> texts = rowTexts(deck.cards[index], open, fontSize, palette);
        double content = 0;
        for (const DeckText &t : texts)
            content += textHeight(t, textWidth);
        content += std::max(0, int(texts.size()) - 1) * m.answerGap;
        const double rowHeight = std::ceil(content + 2 * m.rowVerticalPadding);
        rows.append(QRectF(header.left(), y, header.width(), rowHeight));
        y += rowHeight + m.rowSpacing;
    }
    if (deck.cards.isEmpty())
        y += m.questionLineHeight + m.rowSpacing;
    height = std::ceil(y - m.rowSpacing + m.padding);
}

QString DeckLayout::title(const CardDeck &deck)
{
    return deck.title && !deck.title->isEmpty() ? *deck.title : QStringLiteral("Flashcards");
}

QString DeckLayout::countText(int count)
{
    return count == 1 ? QStringLiteral("1 card") : QStringLiteral("%1 cards").arg(count);
}

QList<DeckText> DeckLayout::rowTexts(const Flashcard &card, bool revealed, double fontSize, const BlueprintPalette &palette)
{
    const Metrics m = metrics(fontSize);
    QList<DeckText> texts;
    texts.append({card.question.isEmpty() ? QStringLiteral(" ") : card.question, fonts::system(m.questionSize, QFont::Medium), palette.text});
    if (!revealed)
        return texts;
    texts.append({card.answer.isEmpty() ? QStringLiteral("No answer") : card.answer, fonts::system(m.answerSize), palette.muted});
    if (card.ref && !card.ref->isEmpty())
        texts.append({*card.ref, fonts::system(m.refSize), withAlpha(palette.muted, 0.75)});
    return texts;
}

double DeckLayout::textHeight(const DeckText &text, double width)
{
    const QFontMetricsF metrics(text.font);
    return std::ceil(metrics.boundingRect(QRectF(0, 0, width, 100000), Qt::TextWordWrap, text.text).height());
}

// MARK: DeckButton

DeckButton::DeckButton(const QString &title, bool playSymbol, const BlueprintPalette &palette, QWidget *parent)
    : QAbstractButton(parent), m_playSymbol(playSymbol), m_palette(palette)
{
    setText(title);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

double DeckButton::preferredWidth(double height) const
{
    const double titleWidth = QFontMetricsF(font()).horizontalAdvance(text());
    const double symbolWidth = m_playSymbol ? font().pixelSize() * 0.75 + 5 : 0;
    return std::ceil(titleWidth + symbolWidth + height * 0.9);
}

void DeckButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double alpha = !isEnabled() ? 0.06 : isDown() ? 0.28 : m_hovered ? 0.2 : 0.13;
    p.setPen(Qt::NoPen);
    p.setBrush(withAlpha(m_palette.text, alpha));
    p.drawRoundedRect(QRectF(rect()), height() / 2.0, height() / 2.0);
    const QColor color = isEnabled() ? m_palette.text : withAlpha(m_palette.muted, 0.5);
    const QFontMetricsF metrics(font());
    const double titleWidth = metrics.horizontalAdvance(text());
    const double symbol = m_playSymbol ? font().pixelSize() * 0.75 : 0;
    const double total = titleWidth + (m_playSymbol ? symbol + 5 : 0);
    double x = std::round((width() - total) / 2);
    if (m_playSymbol) {
        QPainterPath triangle;
        const double cy = height() / 2.0;
        triangle.moveTo(x, cy - symbol / 2);
        triangle.lineTo(x + symbol * 0.85, cy);
        triangle.lineTo(x, cy + symbol / 2);
        triangle.closeSubpath();
        p.setBrush(color);
        p.drawPath(triangle);
        x += symbol + 5;
    }
    p.setFont(font());
    p.setPen(color);
    p.drawText(QPointF(x, std::round((height() - metrics.height()) / 2 + metrics.ascent())), text());
}

void DeckButton::enterEvent(QEnterEvent *)
{
    m_hovered = true;
    update();
}

void DeckButton::leaveEvent(QEvent *)
{
    m_hovered = false;
    update();
}

// MARK: DeckBlockView

DeckBlockView::DeckBlockView(BlockID blockID, const CardDeck &deck, const BlueprintPalette &palette, double fontSize, QWidget *parent)
    : QWidget(parent), m_blockID(blockID), m_palette(palette), m_deck(deck), m_fontSize(fontSize)
{
    m_study = new DeckButton(QStringLiteral("Study"), true, palette, this);
    m_edit = new DeckButton(QStringLiteral("Edit"), false, palette, this);
    connect(m_study, &QAbstractButton::clicked, this, [this] {
        if (m_delegate)
            m_delegate->deckBlockViewRequestsStudy(this);
    });
    connect(m_edit, &QAbstractButton::clicked, this, [this] {
        if (m_delegate)
            m_delegate->deckBlockViewRequestsEditor(this);
    });
    m_study->setAccessibleName(QStringLiteral("Study flashcards"));
    m_edit->setAccessibleName(QStringLiteral("Edit flashcards"));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    changed();
}

void DeckBlockView::setDeck(const CardDeck &deck)
{
    if (deck == m_deck)
        return;
    const bool cardsChanged = deck.cards != m_deck.cards;
    m_deck = deck;
    if (cardsChanged)
        m_revealed.clear();
    changed();
}

void DeckBlockView::setFontSize(double size)
{
    if (size == m_fontSize)
        return;
    m_fontSize = size;
    changed();
}

void DeckBlockView::changed()
{
    m_cache.reset();
    m_study->setEnabled(!m_deck.cards.isEmpty());
    setAccessibleName(QStringLiteral("Flashcards: %1, %2").arg(DeckLayout::title(m_deck), DeckLayout::countText(int(m_deck.cards.size()))));
    layoutButtons();
    update();
}

double DeckBlockView::heightForWidth(double width) const
{
    return layoutFor(width).height;
}

double DeckBlockView::estimatedHeight(const CardDeck &deck, double width, double fontSize)
{
    return DeckLayout(deck, width, fontSize, {}).height;
}

const DeckLayout &DeckBlockView::layoutFor(double width) const
{
    if (!m_cache || m_cache->width != width)
        m_cache = DeckLayout(m_deck, width, m_fontSize, m_revealed);
    return *m_cache;
}

void DeckBlockView::toggleAnswer(int index)
{
    if (index < 0 || index >= m_deck.cards.size())
        return;
    if (m_revealed.contains(index))
        m_revealed.remove(index);
    else
        m_revealed.insert(index);
    m_cache.reset();
    update();
    if (m_delegate)
        m_delegate->deckBlockViewDidChangeHeight(this);
}

void DeckBlockView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutButtons();
}

void DeckBlockView::layoutButtons()
{
    const DeckLayout &layout = layoutFor(width());
    const double height = std::round(DeckLayout::metrics(m_fontSize).buttonHeight);
    double x = layout.header.right();
    for (DeckButton *button : {m_edit, m_study}) {
        QFont f = fonts::system(std::round(m_fontSize * 0.82), QFont::Medium);
        button->setFont(f);
        const double w = button->preferredWidth(height);
        x -= w;
        button->setGeometry(QRect(int(x), int(std::round(layout.header.center().y() - height / 2)), int(w), int(height)));
        x -= 6;
    }
}

void DeckBlockView::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const DeckLayout &layout = layoutFor(width());
    const DeckLayout::Metrics metrics = DeckLayout::metrics(m_fontSize);
    const bool selected = hasFocus();
    const QRectF container = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(withAlpha(m_palette.text, 0.04));
    p.drawRoundedRect(container, 8, 8);
    if (selected) {
        p.setBrush(withAlpha(m_palette.accent, 0.1));
        p.drawRoundedRect(container, 8, 8);
        p.setPen(QPen(m_palette.accent, 1.5));
    } else {
        p.setPen(QPen(withAlpha(m_palette.text, 0.14), 1));
    }
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(container, 8, 8);

    // Header: title and the muted count, truncated before the buttons.
    const QFont titleFont = fonts::system(m_fontSize, QFont::DemiBold);
    const QFont countFont = fonts::system(std::round(m_fontSize * 0.87));
    const QString title = QString::fromUtf8("\xF0\x9F\x97\x82  ") + DeckLayout::title(m_deck);
    const QString count = QStringLiteral("  ·  ") + DeckLayout::countText(int(m_deck.cards.size()));
    const double available = std::max(0.0, m_study->geometry().left() - layout.header.left() - 8);
    const QFontMetricsF titleMetrics(titleFont), countMetrics(countFont);
    const double countWidth = countMetrics.horizontalAdvance(count);
    const QString shownTitle = titleMetrics.elidedText(title, Qt::ElideRight, std::max(0.0, available - countWidth));
    const double titleWidth = titleMetrics.horizontalAdvance(shownTitle);
    const double baseline = layout.header.center().y() + (titleMetrics.ascent() - titleMetrics.descent()) / 2;
    p.setFont(titleFont);
    p.setPen(m_palette.text);
    p.drawText(QPointF(layout.header.left(), baseline), shownTitle);
    if (titleWidth + countWidth <= available + 0.5) {
        p.setFont(countFont);
        p.setPen(m_palette.muted);
        p.drawText(QPointF(layout.header.left() + titleWidth, baseline), count);
    }

    if (m_deck.cards.isEmpty()) {
        p.setFont(fonts::system(metrics.answerSize));
        p.setPen(m_palette.muted);
        p.drawText(QPointF(layout.header.left(), layout.header.bottom() + metrics.rowSpacing + QFontMetricsF(p.font()).ascent()),
                   QStringLiteral("No cards yet. Click Edit to add some."));
        return;
    }
    for (int index = 0; index < layout.rows.size(); ++index) {
        if (layout.rows[index].intersects(event->rect()))
            drawRow(p, index, layout.rows[index], metrics);
    }
}

void DeckBlockView::drawRow(QPainter &p, int index, const QRectF &rect, const DeckLayout::Metrics &metrics)
{
    const Flashcard &card = m_deck.cards[index];
    const bool open = m_revealed.contains(index);
    p.setPen(Qt::NoPen);
    p.setBrush(withAlpha(m_palette.text, open ? 0.1 : 0.07));
    p.drawRoundedRect(rect, 6, 6);
    const QRectF content = rect.adjusted(metrics.rowPadding, metrics.rowVerticalPadding, -metrics.rowPadding, -metrics.rowVerticalPadding);
    const QList<DeckText> texts = DeckLayout::rowTexts(card, open, m_fontSize, m_palette);
    const double textWidth = content.width() - (open ? 0 : metrics.hintWidth);
    double y = content.top();
    for (int i = 0; i < texts.size(); ++i) {
        if (i > 0)
            y += metrics.answerGap;
        const double h = DeckLayout::textHeight(texts[i], textWidth);
        p.setFont(texts[i].font);
        p.setPen(texts[i].color);
        p.drawText(QRectF(content.left(), y, textWidth, h), Qt::TextWordWrap | Qt::AlignTop | Qt::AlignLeft, texts[i].text);
        y += h;
    }
    if (!open) {
        const QFont hintFont = fonts::system(metrics.refSize, QFont::Medium);
        const QFontMetricsF hm(hintFont);
        const QString hint = QStringLiteral("Show");
        p.setFont(hintFont);
        p.setPen(withAlpha(m_palette.muted, 0.7));
        p.drawText(QPointF(content.right() - hm.horizontalAdvance(hint),
                           content.top() + (metrics.questionLineHeight - hm.height()) / 2 + hm.ascent()),
                   hint);
    }
}

void DeckBlockView::click(const QPointF &point, bool doubleClick)
{
    setFocus(Qt::MouseFocusReason);
    const DeckLayout &layout = layoutFor(width());
    for (int row = 0; row < layout.rows.size(); ++row) {
        if (layout.rows[row].contains(point)) {
            toggleAnswer(row);
            return;
        }
    }
    if (doubleClick && m_delegate)
        m_delegate->deckBlockViewRequestsEditor(this);
}

void DeckBlockView::mousePressEvent(QMouseEvent *event)
{
    click(event->position(), false);
}

void DeckBlockView::mouseDoubleClickEvent(QMouseEvent *event)
{
    click(event->position(), true);
}

void DeckBlockView::keyPressEvent(QKeyEvent *event)
{
    if (!m_delegate || !m_delegate->deckBlockViewHandleKey(this, event))
        QWidget::keyPressEvent(event);
}

void DeckBlockView::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    update();
}

void DeckBlockView::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    update();
}

} // namespace wp
