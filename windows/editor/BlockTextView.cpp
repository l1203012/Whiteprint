#include "editor/BlockTextView.h"

#include "editor/MarkdownConcealment.h"
#include "editor/MarkdownEditing.h"
#include "editor/PageGeometry.h"
#include "render/Fonts.h"

#include <QAbstractTextDocumentLayout>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <algorithm>
#include <cmath>

namespace wp {

namespace TextStyle {

QTextCharFormat base(const BlueprintPalette &palette, double fontSize)
{
    QTextCharFormat format;
    format.setFont(fonts::system(fontSize));
    format.setForeground(QBrush(palette.text));
    return format;
}

double lineHeight(double fontSize)
{
    return std::round(HeightEstimate::bodyLineHeight * fontSize / MarkdownStyler::defaultFontSize);
}

} // namespace TextStyle

BlockTextView::BlockTextView(BlockID blockID, const QString &text, const BlueprintPalette &palette, double width, double fontSize,
                             bool concealsMarkup, QWidget *parent)
    : QTextEdit(parent), m_blockID(blockID), m_palette(palette), m_fontSize(fontSize), m_conceals(concealsMarkup)
{
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAcceptRichText(false);
    setUndoRedoEnabled(false);
    setTabChangesFocus(false);
    document()->setDocumentMargin(0);
    document()->setUndoRedoEnabled(false);
    document()->setDefaultFont(fonts::system(fontSize));
    setLineWrapMode(QTextEdit::FixedPixelWidth);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    viewport()->setAutoFillBackground(false);
    setAutoFillBackground(false);
    applyPaletteColors();
    setAccessibleName(QStringLiteral("Text block"));

    connect(document(), &QTextDocument::contentsChange, this, &BlockTextView::onContentsChange);
    connect(this, &QTextEdit::cursorPositionChanged, this, &BlockTextView::onSelectionChanged);
    connect(this, &QTextEdit::selectionChanged, this, &BlockTextView::onSelectionChanged);

    setFixedWidth(int(width));
    setLineWrapColumnOrWidth(int(width));
    setFixedHeight(int(TextStyle::lineHeight(fontSize)));
    setText(text);
}

void BlockTextView::setBlueprintPalette(const BlueprintPalette &palette)
{
    if (m_palette == palette)
        return;
    m_palette = palette;
    applyPaletteColors();
    restyle();
}

void BlockTextView::applyPaletteColors()
{
    QPalette p = this->palette();
    p.setColor(QPalette::Base, Qt::transparent);
    p.setColor(QPalette::Text, m_palette.text);
    p.setColor(QPalette::Highlight, withAlpha(m_palette.accent, 0.32));
    p.setColor(QPalette::HighlightedText, m_palette.text);
    setPalette(p);
    setCurrentCharFormat(TextStyle::base(m_palette, m_fontSize));
}

void BlockTextView::setAlonePlaceholder(bool alone)
{
    if (m_alone == alone)
        return;
    m_alone = alone;
    viewport()->update();
}

void BlockTextView::setFontSize(double size)
{
    if (m_fontSize == size)
        return;
    m_fontSize = size;
    document()->setDefaultFont(fonts::system(size));
    setCurrentCharFormat(TextStyle::base(m_palette, size));
    restyle();
}

void BlockTextView::setConcealsMarkup(bool conceals)
{
    if (m_conceals == conceals)
        return;
    m_conceals = conceals;
    m_revealed = revealedParagraphs();
    restyle();
}

TextRange BlockTextView::selectedRange() const
{
    const QTextCursor c = textCursor();
    return TextRange(c.selectionStart(), c.selectionEnd() - c.selectionStart());
}

void BlockTextView::setSelectedRange(TextRange range)
{
    const int length = int(m_text.size());
    const int start = std::min(std::max(range.location, 0), length);
    const int end = std::min(std::max(range.end(), start), length);
    QTextCursor c(document());
    c.setPosition(start);
    c.setPosition(end, QTextCursor::KeepAnchor);
    setTextCursor(c);
}

void BlockTextView::ensureLayout() const
{
    document()->documentLayout()->documentSize();
}

void BlockTextView::setText(const QString &text)
{
    if (m_text == text && toPlainText() == text)
        return;
    const TextRange selection = selectedRange();
    m_syncing = true;
    document()->setPlainText(text);
    m_text = toPlainText();
    style(std::nullopt);
    m_syncing = false;
    setSelectedRange(selection);
    updateRevealedParagraphs();
    sizeToFit();
}

void BlockTextView::applyReplace(TextRange range, const QString &replacement)
{
    QTextCursor c(document());
    c.setPosition(range.location);
    c.setPosition(range.end(), QTextCursor::KeepAnchor);
    c.insertText(replacement, TextStyle::base(m_palette, m_fontSize));
}

void BlockTextView::replaceSilently(TextRange range, const QString &replacement)
{
    m_syncing = true;
    applyReplace(range, replacement);
    const QString old = m_text;
    m_text = toPlainText();
    if (m_revealed)
        m_revealed = BlockTextView::range(*m_revealed, TextRange(range.location, int(replacement.size())),
                                          int(replacement.size()) - range.length, m_text);
    style(TextRange(range.location, int(replacement.size())));
    m_syncing = false;
    updateRevealedParagraphs();
    sizeToFit();
}

void BlockTextView::replaceText(TextRange range, const QString &replacement)
{
    applyReplace(range, replacement);
}

// MARK: Styling

void BlockTextView::onContentsChange(int from, int removed, int added)
{
    if (m_restyling || m_syncing)
        return;
    const QString text = toPlainText();
    if (text == m_text)
        return; // formats only
    const QString old = m_text;
    m_text = text;
    const TextEdit edit{from, old.mid(from, removed), text.mid(from, added)};
    if (m_revealed)
        m_revealed = range(*m_revealed, TextRange(from, added), added - removed, text);
    const bool whole = added == text.size();
    style(whole ? std::nullopt : std::optional<TextRange>(TextRange(from, added)));
    sizeToFit();
    updateRevealedParagraphs();
    if (m_delegate)
        m_delegate->blockTextViewDidEdit(this, edit);
}

void BlockTextView::style(std::optional<TextRange> range)
{
    const bool wasRestyling = m_restyling;
    m_restyling = true;
    MarkdownStyler::apply(document(), range, m_palette, m_fontSize, m_conceals, m_revealed);
    ConcealedGlyphs::applyZeroWidth(document());
    m_restyling = wasRestyling;
    viewport()->update();
}

void BlockTextView::restyle()
{
    style(std::nullopt);
    sizeToFit();
}

std::optional<TextRange> BlockTextView::revealedParagraphs() const
{
    if (!m_conceals || !m_hasFocus)
        return std::nullopt;
    return paragraphRange(m_text, selectedRange());
}

void BlockTextView::updateRevealedParagraphs()
{
    const auto revealed = revealedParagraphs();
    if (revealed == m_revealed)
        return;
    const auto previous = m_revealed;
    m_revealed = revealed;
    const int length = int(m_text.size());
    for (const auto &range : {previous, revealed}) {
        if (!range)
            continue;
        const int location = std::min(range->location, length);
        style(TextRange(location, std::min(range->length, length - location)));
    }
    sizeToFit();
}

TextRange BlockTextView::range(TextRange range, TextRange edited, int delta, const QString &text)
{
    const int oldEditEnd = edited.end() - delta;
    if (oldEditEnd < range.location)
        return TextRange(range.location + delta, range.length);
    if (edited.location > range.end())
        return range;
    const int length = int(text.size());
    const int start = std::min({range.location, edited.location, length});
    const int end = std::min(length, std::max(range.end() + delta, edited.end()));
    return paragraphRange(text, TextRange(start, std::max(0, end - start)));
}

void BlockTextView::onSelectionChanged()
{
    if (m_restyling || m_syncing)
        return;
    if (toPlainText() != m_text)
        return; // an edit is being processed; it reports afterwards
    updateRevealedParagraphs();
    if (m_delegate)
        m_delegate->blockTextViewDidChangeSelection(this);
}

// MARK: Geometry

void BlockTextView::setWrapWidth(int width)
{
    if (this->width() != width) {
        setFixedWidth(width);
        setLineWrapColumnOrWidth(width);
    }
    sizeToFit();
}

void BlockTextView::sizeToFit()
{
    ensureLayout();
    const double contents = document()->documentLayout()->documentSize().height();
    const int height = int(std::max(TextStyle::lineHeight(m_fontSize), std::ceil(contents)));
    if (height != this->height()) {
        setFixedHeight(height);
        if (m_delegate)
            m_delegate->blockTextViewDidChangeHeight(this);
    }
}

QRectF BlockTextView::lineRect(int index) const
{
    ensureLayout();
    QTextCursor c(document());
    c.setPosition(std::min(std::max(index, 0), int(m_text.size())));
    return QRectF(cursorRect(c));
}

QRectF BlockTextView::rect(TextRange range) const
{
    const QRectF a = lineRect(range.location);
    const QRectF b = lineRect(range.end());
    if (std::abs(a.top() - b.top()) < 1)
        return QRectF(a.left(), a.top(), std::max(0.0, b.left() - a.left()), a.height());
    return a.united(b);
}

bool BlockTextView::caretIsOnFirstLine() const
{
    return lineRect(selectedRange().location).top() <= lineRect(0).top() + 0.5;
}

bool BlockTextView::caretIsOnLastLine() const
{
    return lineRect(selectedRange().end()).bottom() >= lineRect(int(m_text.size())).bottom() - 0.5;
}

double BlockTextView::caretX() const
{
    const int index = selectedRange().location;
    const int length = int(m_text.size());
    if (index < length && m_text[index] != QLatin1Char('\n'))
        return rect(TextRange(index, 1)).left();
    if (index > 0 && m_text[index - 1] != QLatin1Char('\n'))
        return rect(TextRange(index - 1, 1)).right();
    return 0;
}

void BlockTextView::placeCaret(double x, bool onFirstLine)
{
    const QRectF line = lineRect(onFirstLine ? 0 : int(m_text.size()));
    const int index = cursorForPosition(QPoint(int(x), int(line.center().y()))).position();
    setSelectedRange(TextRange(std::min(index, int(m_text.size())), 0));
}

std::optional<TextRange> BlockTextView::checkbox(const QPointF &point) const
{
    const int index = cursorForPosition(point.toPoint()).position();
    const auto box = MarkdownEditing::checkboxRange(index, m_text);
    if (!box)
        return std::nullopt;
    return rect(*box).adjusted(-4, -3, 4, 3).contains(point) ? box : std::nullopt;
}

// MARK: Events

void BlockTextView::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo)) {
        if (m_delegate)
            m_delegate->blockTextViewUndoRequested(this, event->matches(QKeySequence::Redo));
        event->accept();
        return;
    }
    if (m_delegate) {
        const QString text = event->text();
        const bool modified = event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        if (!modified && !text.isEmpty() && text[0].isPrint()) {
            if (m_delegate->blockTextViewWillInsert(this, text, selectedRange())) {
                event->accept();
                return;
            }
        } else if (m_delegate->blockTextViewHandleKey(this, event)) {
            event->accept();
            return;
        }
    }
    QTextEdit::keyPressEvent(event);
}

void BlockTextView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_delegate) {
        if (const auto box = checkbox(event->position())) {
            m_delegate->blockTextViewToggleCheckbox(this, box->location);
            event->accept();
            return;
        }
    }
    QTextEdit::mousePressEvent(event);
}

void BlockTextView::wheelEvent(QWheelEvent *event)
{
    event->ignore();
}

void BlockTextView::focusInEvent(QFocusEvent *event)
{
    QTextEdit::focusInEvent(event);
    m_hasFocus = true;
    viewport()->update();
    updateRevealedParagraphs();
    if (m_delegate)
        m_delegate->blockTextViewDidBecomeFocused(this);
}

void BlockTextView::focusOutEvent(QFocusEvent *event)
{
    QTextEdit::focusOutEvent(event);
    m_hasFocus = false;
    viewport()->update();
    updateRevealedParagraphs();
}

bool BlockTextView::canInsertFromMimeData(const QMimeData *source) const
{
    return source->hasText();
}

void BlockTextView::insertFromMimeData(const QMimeData *source)
{
    if (source->hasText())
        textCursor().insertText(source->text(), TextStyle::base(m_palette, m_fontSize));
}

QMimeData *BlockTextView::createMimeDataFromSelection() const
{
    auto *mime = new QMimeData;
    mime->setText(textCursor().selection().toPlainText());
    return mime;
}

void BlockTextView::paintEvent(QPaintEvent *event)
{
    QTextEdit::paintEvent(event);
    QPainter painter(viewport());
    if (m_text.isEmpty() && (m_alone || m_hasFocus)) {
        painter.setFont(fonts::system(m_fontSize));
        painter.setPen(withAlpha(m_palette.muted, 0.55));
        const QRectF line = lineRect(0);
        painter.drawText(QPointF(line.left(), line.top() + QFontMetricsF(painter.font()).ascent()), placeholderText());
    }
    ConcealedGlyphs::draw(painter, document(), m_palette, QRectF(event->rect()));
}

} // namespace wp
