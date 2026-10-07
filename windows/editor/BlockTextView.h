#pragma once
#include "editor/EditorDocument.h"
#include "render/MarkdownStyler.h"
#include "render/Palette.h"

#include <QTextCharFormat>
#include <QTextEdit>
#include <optional>

namespace wp {

class BlockTextView;

/// A change to a block's text: `removed` at `position` was replaced by `added`.
struct TextEdit {
    int position = 0;
    QString removed;
    QString added;
};

/// What a text block reports back to the editor.
class BlockTextViewDelegate
{
public:
    virtual ~BlockTextViewDelegate() = default;
    virtual void blockTextViewDidChangeHeight(BlockTextView *view) = 0;
    virtual void blockTextViewToggleCheckbox(BlockTextView *view, int location) = 0;
    virtual void blockTextViewDidBecomeFocused(BlockTextView *view) = 0;
    /// Text is about to be typed over `range`. Return true to handle it (the view inserts nothing).
    virtual bool blockTextViewWillInsert(BlockTextView *view, const QString &string, TextRange range) = 0;
    /// A command key (Return, Tab, arrows, Backspace, Escape...). Return true when handled.
    virtual bool blockTextViewHandleKey(BlockTextView *view, QKeyEvent *event) = 0;
    /// The text changed (not by `setText`/`replaceSilently`); styling is already up to date.
    virtual void blockTextViewDidEdit(BlockTextView *view, const TextEdit &edit) = 0;
    virtual void blockTextViewDidChangeSelection(BlockTextView *view) = 0;
    virtual void blockTextViewUndoRequested(BlockTextView *view, bool redo) = 0;
};

/// Base attributes for editor text; `MarkdownStyler` refines them per paragraph.
namespace TextStyle {

QTextCharFormat base(const BlueprintPalette &palette, double fontSize = MarkdownStyler::defaultFontSize);
/// The minimum height of a line of body text.
double lineHeight(double fontSize);

} // namespace TextStyle

/// One run of Markdown text: a transparent, auto-height text edit in the
/// page's colours, restyled by `MarkdownStyler` as it's edited. Its own undo is
/// off; the editor keeps one undo stack for everything.
///
/// With `concealsMarkup`, the markup of every paragraph but the ones holding
/// the caret or selection is hidden (see `MarkdownConcealment.h`).
class BlockTextView : public QTextEdit
{
    Q_OBJECT

public:
    static QString placeholderText() { return QStringLiteral("Type / for commands"); }

    BlockTextView(BlockID blockID, const QString &text, const BlueprintPalette &palette, double width,
                  double fontSize = MarkdownStyler::defaultFontSize, bool concealsMarkup = false, QWidget *parent = nullptr);

    BlockID blockID() const { return m_blockID; }
    const BlueprintPalette &blueprintPalette() const { return m_palette; }
    /// Changes the colours and restyles the text in place.
    void setBlueprintPalette(const BlueprintPalette &palette);
    void setDelegate(BlockTextViewDelegate *delegate) { m_delegate = delegate; }

    /// Shows the placeholder while empty even when not focused (an empty page).
    bool isAlonePlaceholder() const { return m_alone; }
    void setAlonePlaceholder(bool alone);

    double fontSize() const { return m_fontSize; }
    void setFontSize(double size);
    bool concealsMarkup() const { return m_conceals; }
    void setConcealsMarkup(bool conceals);
    /// The paragraphs whose markup shows while concealing, as last styled.
    std::optional<TextRange> revealedRange() const { return m_revealed; }

    /// The text (raw Markdown).
    const QString &string() const { return m_text; }
    TextRange selectedRange() const;
    void setSelectedRange(TextRange range);

    /// Replaces the whole text without notifying the delegate (for model to view syncs).
    void setText(const QString &text);
    /// Replaces `range` without notifying the delegate (for undo).
    void replaceSilently(TextRange range, const QString &replacement);
    /// Replaces `range`; the delegate hears about it like any other edit.
    void replaceText(TextRange range, const QString &replacement);

    /// Sets the width text wraps at, and resizes to fit the text.
    void setWrapWidth(int width);
    void sizeToFit();

    /// The line the caret at `index` sits on, in view coordinates.
    QRectF lineRect(int index) const;
    /// The rect covering a character range, in view coordinates.
    QRectF rect(TextRange range) const;
    bool caretIsOnFirstLine() const;
    bool caretIsOnLastLine() const;
    /// The caret's horizontal position, kept when moving between blocks.
    double caretX() const;
    /// Puts the caret on the first or last line, as near to `x` as possible.
    void placeCaret(double x, bool onFirstLine);

    /// The checkbox under `point`, if any.
    std::optional<TextRange> checkbox(const QPointF &point) const;

    /// Where `range` is after an edit that left `edited` (in the new text)
    /// and changed the length by `delta`: moved along, untouched, or grown to
    /// the paragraphs of both when they meet.
    static TextRange range(TextRange range, TextRange edited, int delta, const QString &text);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
    QMimeData *createMimeDataFromSelection() const override;
    bool canInsertFromMimeData(const QMimeData *source) const override;

private:
    void onContentsChange(int from, int removed, int added);
    void onSelectionChanged();
    void applyPaletteColors();
    void style(std::optional<TextRange> range);
    void restyle();
    std::optional<TextRange> revealedParagraphs() const;
    void updateRevealedParagraphs();
    void applyReplace(TextRange range, const QString &replacement);
    void ensureLayout() const;

    BlockID m_blockID;
    BlueprintPalette m_palette;
    BlockTextViewDelegate *m_delegate = nullptr;
    QString m_text;
    bool m_alone = false;
    double m_fontSize;
    bool m_conceals;
    std::optional<TextRange> m_revealed;
    bool m_hasFocus = false;
    bool m_restyling = false;
    bool m_syncing = false;
};

} // namespace wp
