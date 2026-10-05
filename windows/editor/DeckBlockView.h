#pragma once
#include "core/CardDeck.h"
#include "editor/EditorDocument.h"
#include "render/MarkdownStyler.h"
#include "render/Palette.h"

#include <QAbstractButton>
#include <QColor>
#include <QFont>
#include <QList>
#include <QRectF>
#include <QSet>
#include <QWidget>
#include <optional>

class QKeyEvent;

namespace wp {

class DeckBlockView;

class DeckBlockViewDelegate
{
public:
    virtual ~DeckBlockViewDelegate() = default;
    virtual void deckBlockViewRequestsEditor(DeckBlockView *view) = 0;
    virtual void deckBlockViewRequestsStudy(DeckBlockView *view) = 0;
    virtual bool deckBlockViewHandleKey(DeckBlockView *view, QKeyEvent *event) = 0;
    virtual void deckBlockViewDidChangeHeight(DeckBlockView *view) = 0;
};

/// A piece of text with its look, for the deck's rows.
struct DeckText {
    QString text;
    QFont font;
    QColor color;
};

/// Where a deck block's header and card rows go for a column width.
struct DeckLayout {
    struct Metrics {
        double padding, headerHeight, buttonHeight, rowSpacing, rowPadding, rowVerticalPadding, answerGap, hintWidth;
        double questionSize, answerSize, refSize, questionLineHeight;
    };

    static Metrics metrics(double fontSize);

    double width = 0;
    QRectF header;
    QList<QRectF> rows;
    double height = 0;

    DeckLayout() = default;
    DeckLayout(const CardDeck &deck, double width, double fontSize, const QSet<int> &revealed);

    static QString title(const CardDeck &deck);
    static QString countText(int count);
    /// The question, and with the answer revealed, the answer and source.
    static QList<DeckText> rowTexts(const Flashcard &card, bool revealed, double fontSize, const BlueprintPalette &palette);
    static double textHeight(const DeckText &text, double width);
};

/// A small pill button for the deck header, drawn for the blueprint page.
class DeckButton : public QAbstractButton
{
    Q_OBJECT

public:
    DeckButton(const QString &title, bool playSymbol, const BlueprintPalette &palette, QWidget *parent = nullptr);
    double preferredWidth(double height) const;

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    bool m_playSymbol;
    BlueprintPalette m_palette;
    bool m_hovered = false;
};

/// A flashcard deck on the page: a header (title, card count, Study and
/// Edit) above the cards as compact rows. Clicking a row reveals or hides
/// its answer; the block is selected (focused) like a drawing.
class DeckBlockView : public QWidget
{
    Q_OBJECT

public:
    DeckBlockView(BlockID blockID, const CardDeck &deck, const BlueprintPalette &palette,
                  double fontSize = MarkdownStyler::defaultFontSize, QWidget *parent = nullptr);

    BlockID blockID() const { return m_blockID; }
    void setDelegate(DeckBlockViewDelegate *delegate) { m_delegate = delegate; }

    const CardDeck &deck() const { return m_deck; }
    void setDeck(const CardDeck &deck);
    double fontSize() const { return m_fontSize; }
    void setFontSize(double size);
    /// Indices of the cards showing their answers.
    const QSet<int> &revealed() const { return m_revealed; }

    /// Height of the block for a column of `width`.
    double heightForWidth(double width) const;
    /// Height of a deck with no answers shown, for pages not laid out yet.
    static double estimatedHeight(const CardDeck &deck, double width, double fontSize);
    /// Shows or hides the answer of card `index`.
    void toggleAnswer(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;

private:
    void changed();
    const DeckLayout &layoutFor(double width) const;
    void layoutButtons();
    void drawRow(QPainter &p, int index, const QRectF &rect, const DeckLayout::Metrics &metrics);
    void click(const QPointF &point, bool doubleClick);

    BlockID m_blockID;
    DeckBlockViewDelegate *m_delegate = nullptr;
    BlueprintPalette m_palette;
    CardDeck m_deck;
    double m_fontSize;
    DeckButton *m_study;
    DeckButton *m_edit;
    QSet<int> m_revealed;
    mutable std::optional<DeckLayout> m_cache;
};

} // namespace wp
