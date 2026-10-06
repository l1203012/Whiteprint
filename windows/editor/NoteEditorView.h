#pragma once
#include "core/Note.h"
#include "editor/BlockHandleView.h"
#include "editor/BlockTextView.h"
#include "editor/ChangeCoalescer.h"
#include "editor/DeckBlockView.h"
#include "editor/DeckEditor.h"
#include "editor/DrawingBlockView.h"
#include "editor/DrawingSourceEditor.h"
#include "editor/EditorCommand.h"
#include "editor/EditorDocument.h"
#include "editor/EditorToolbar.h"
#include "editor/PageGeometry.h"
#include "editor/PageView.h"
#include "editor/SlashMenuView.h"
#include "render/Palette.h"

#include <QHash>
#include <QUndoStack>
#include <QWidget>
#include <functional>
#include <optional>

class QScrollArea;

namespace wp {

/// The Notion-style editing surface for one note: a scrolling, centred column
/// of blue blueprint pages with white text, inline drawings and flashcard
/// decks, slash menu, Markdown shortcuts and checkboxes.
///
/// The content lives in an `EditorDocument`; views are thin and keyed by
/// block id. Pages near the viewport get views; the rest are estimated.
///
/// # Public API (mirrors the Swift `NoteEditorView`)
///
/// - `NoteEditorView(note, palette)`; `note()`: the current content, including unsaved edits.
/// - `onChange(Note)` / signal `changed(Note)`: after each user edit (coalesced, at most every ~300 ms).
/// - `onVisiblePageChange(int)` / signal `visiblePageChanged(int)`: the page nearest the top of
///   the viewport changed (1-based).
/// - `onStudyDeck(CardDeck)` / signal `studyDeckRequested(CardDeck)`: a deck's Study button was clicked.
/// - `layoutMode()` / `setLayoutMode(PageLayoutMode)`: slides (default) or a4. Keeps the top block in place.
/// - `showsMarkdownSyntax()` / `setShowsMarkdownSyntax(bool)`: false hides markup except in the
///   paragraphs holding the caret or selection.
/// - `setNote(note, preservingSelection)`: replace the content (e.g. after an MCP edit); one undo step.
/// - `scrollToPage(page)` (1-based), `insertDrawingAtSelection(source)`, `insertDeckAtSelection()`,
///   `addPage()`, `perform(EditorCommand)`.
/// - `undoStack()`: one `QUndoStack` for typing and block edits. Connect Edit > Undo/Redo to
///   `undo()` / `redo()` or use `undoStack()->createUndoAction()`.
/// - `commandActions()`: the optional `EditorToolbar` (QActions for the command set; the Windows
///   stand-in for the Touch Bar), e.g. `commandActions()->makeToolBar()`.
/// - `setPresentsEditors(bool)`: whether the drawing and flashcard editors open as popups (default
///   true when the view is visible; tests turn it off).
///
/// Everything below "Internals" is public for the block menu, the views and the tests.
class NoteEditorView : public QWidget,
                       public BlockTextViewDelegate,
                       public DrawingBlockViewDelegate,
                       public DeckBlockViewDelegate
{
    Q_OBJECT

public:
    explicit NoteEditorView(const Note &note, const BlueprintPalette &palette = BlueprintPalette::blueprint(), QWidget *parent = nullptr);
    ~NoteEditorView() override;

    /// The current content, including unsaved edits.
    const Note &note() const { return m_note; }

    /// Called after each user edit (coalesced, at most every ~300 ms). Also the `changed` signal.
    std::function<void(const Note &)> onChange;
    /// Called when the page nearest the top of the viewport changes (1-based). Also `visiblePageChanged`.
    std::function<void(int)> onVisiblePageChange;
    /// Called when a deck's Study button is clicked. Also `studyDeckRequested`.
    std::function<void(const CardDeck &)> onStudyDeck;

    PageLayoutMode layoutMode() const { return m_layoutMode; }
    /// Slides (the default) or A4 sheets showing where printed pages break.
    /// Switching keeps the block at the top of the viewport in place.
    void setLayoutMode(PageLayoutMode mode);

    bool showsMarkdownSyntax() const { return m_showsMarkdownSyntax; }
    /// Whether Markdown markup shows (the default). When false, it's hidden
    /// except in the paragraphs holding the caret or selection, so they stay
    /// editable; the text is still raw Markdown.
    void setShowsMarkdownSyntax(bool shows);

    /// Replaces the content, e.g. after Claude edited the note over MCP. With
    /// `preservingSelection`, the caret and scroll position stay where they were
    /// as far as possible. The change is one undoable step.
    void setNote(const Note &note, bool preservingSelection);

    /// 1-based.
    void scrollToPage(int page);
    /// Inserts a drawing at the caret (or the end of the current page) and opens its source editor.
    void insertDrawingAtSelection(const QString &source = QString());
    /// Inserts an empty flashcard deck at the caret (or the end of the current page) and opens its editor.
    void insertDeckAtSelection();
    /// Adds a page after the current one and moves the caret there.
    void addPage();
    /// Runs a formatting command on the selection, or inserts a block.
    /// Text commands need a focused text block; each is one undoable step.
    void perform(EditorCommand command);

    // Undo and redo (typing and block edits share one stack).
    QUndoStack *undoStack() const { return m_undoStack; }
    bool canUndo() const { return m_undoStack->canUndo(); }
    bool canRedo() const { return m_undoStack->canRedo(); }
    QString undoActionName() const { return m_undoStack->undoText(); }
    QString redoActionName() const { return m_undoStack->redoText(); }
    void undo();
    void redo();

    /// The command set as QActions / a toolbar (created on first use).
    EditorToolbar *commandActions();

    void setPresentsEditors(bool presents) { m_presentsEditors = presents; }

    /// Delivers a pending `onChange` immediately.
    void flushPendingChange();
    /// Runs a pending layout now.
    void layoutSubtreeIfNeeded();

signals:
    void changed(const wp::Note &note);
    void visiblePageChanged(int page);
    void studyDeckRequested(const wp::CardDeck &deck);

public:
    // MARK: Internals

    /// Where the keyboard is: a caret or selection in a text block, or a selected drawing or deck.
    struct Focus {
        enum class Kind { text, block };
        Kind kind = Kind::block;
        BlockID id;
        TextRange range;

        static Focus text(BlockID id, TextRange range) { return Focus{Kind::text, id, range}; }
        static Focus block(BlockID id) { return Focus{Kind::block, id, TextRange()}; }
        bool operator==(const Focus &) const = default;
    };

    struct Snapshot {
        EditorDocument document;
        std::optional<Focus> focus;
    };

    struct ScrollAnchor {
        int page = 0;
        double offset = 0;
    };

    struct SlashMenu {
        BlockID block;
        SlashMenuState state;
    };

    const BlueprintPalette &blueprintPalette() const { return m_palette; }
    const EditorDocument &document() const { return m_document; }
    QScrollArea *scrollArea() const { return m_scroll; }
    EditorDocumentView *documentView() const { return m_documentView; }
    SlashMenuView *slashMenuView() const { return m_slashMenuView; }
    const std::optional<SlashMenu> &slashMenu() const { return m_slashMenu; }
    DrawingSourceEditor *drawingEditor() const { return m_drawingEditor; }
    DeckEditor *deckEditor() const { return m_deckEditor; }
    const QList<PageView *> &pageViews() const { return m_pageViews; }
    const QHash<BlockID, BlockTextView *> &textViews() const { return m_textViews; }
    const QHash<BlockID, DrawingBlockView *> &drawingViews() const { return m_drawingViews; }
    const QHash<BlockID, DeckBlockView *> &deckViews() const { return m_deckViews; }
    int visiblePage() const { return m_visiblePage; }
    PageGeometry geometry() const;
    /// The viewport's current top, and scrolling to a document y.
    double scrollTop() const;
    void scrollToY(double y);

    std::optional<Focus> currentFocus() const;
    void focus(const std::optional<Focus> &focus, bool scroll = true);
    int currentPageIndex() const;
    Focus endFocus(const EditorBlock &block) const;

    Snapshot snapshotNow() const { return Snapshot{m_document, currentFocus()}; }
    std::optional<Focus> performBlockEdit(const QString &name, const std::function<std::optional<Focus>(EditorDocument &)> &body);
    void registerUndo(const Snapshot &before, const QString &name);
    void restore(const Snapshot &snapshot);
    /// Replaces `range` of a text block's view and the model without recording undo (used by undo and redo).
    void applyTextEdit(BlockID block, TextRange range, const QString &replacement);

    /// Applies a text change through the text view, so it's undoable as typing.
    void perform(const TextChange &change, BlockTextView *view, const QString &actionName = QString());

    std::optional<std::pair<BlockID, double>> topBlock() const;
    std::optional<ScrollAnchor> scrollAnchor() const;
    void restoreScroll(const std::optional<ScrollAnchor> &anchor);

    void syncViews();
    void realize(int pageIndex);
    void realizeVisiblePages();
    QWidget *viewFor(BlockID id);
    std::optional<BlockID> blockIDOf(QWidget *view) const;
    void layoutDocument();

    // Moving between blocks
    bool moveFocus(BlockID from, bool forward, std::optional<double> x);
    bool backspaceAtStart(BlockID id);

    // Slash menu
    void openSlashMenu(BlockTextView *view, int location);
    void updateSlashMenu(BlockTextView *view);
    void closeSlashMenu();
    void chooseSlashCommand(std::optional<int> row);
    void positionSlashMenu();

    // Block operations
    void insertDrawing(const QString &source, std::optional<BlockID> splitting, std::optional<int> offset, int page);
    void insertDeck(std::optional<BlockID> splitting, std::optional<int> offset, int page);
    void addPageAfter(int page);
    void deleteBlock(BlockID id);
    void duplicateBlock(BlockID id);
    void moveBlock(BlockID id, int delta);
    void showBlockMenu(BlockHandleView *handle);
    bool handleKeyOnSelected(QKeyEvent *event, BlockID id);

    // Drawings and decks
    void editDrawing(BlockID id);
    void closeDrawingEditor(bool commit);
    void commitDrawing(BlockID id, const QString &source);
    void editDeck(BlockID id);
    void closeDeckEditor(bool commit);
    void commitDeck(BlockID id, const CardDeck &deck);

    // Delegates
    void blockTextViewDidChangeHeight(BlockTextView *view) override;
    void blockTextViewToggleCheckbox(BlockTextView *view, int location) override;
    void blockTextViewDidBecomeFocused(BlockTextView *view) override;
    bool blockTextViewWillInsert(BlockTextView *view, const QString &string, TextRange range) override;
    bool blockTextViewHandleKey(BlockTextView *view, QKeyEvent *event) override;
    void blockTextViewDidEdit(BlockTextView *view, const TextEdit &edit) override;
    void blockTextViewDidChangeSelection(BlockTextView *view) override;
    void blockTextViewUndoRequested(BlockTextView *view, bool redo) override;
    void drawingBlockViewRequestsEditor(DrawingBlockView *view) override;
    bool drawingBlockViewHandleKey(DrawingBlockView *view, QKeyEvent *event) override;
    void deckBlockViewRequestsEditor(DeckBlockView *view) override;
    void deckBlockViewRequestsStudy(DeckBlockView *view) override;
    bool deckBlockViewHandleKey(DeckBlockView *view, QKeyEvent *event) override;
    void deckBlockViewDidChangeHeight(DeckBlockView *view) override;

protected:
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setUpViews();
    void applyFontSize(double size);
    double estimatedHeight(const EditorPage &page, const PageGeometry &geometry) const;
    void keepingTopBlockInPlace(const std::function<void()> &change);
    void didScroll();
    void updateVisiblePage();
    PageView *makePageView(PageID id);
    QWidget *blockViewFor(const EditorBlock &block, bool alone);
    BlockTextView *makeTextView(BlockID id, const QString &text);
    DrawingBlockView *makeDrawingView(BlockID id, const QString &source);
    DeckBlockView *makeDeckView(BlockID id, const CardDeck &deck);
    QRectF nearbyRect() const;
    void noteDidChange();
    std::optional<Focus> transferred(const Focus &focus, const EditorDocument &old) const;
    std::optional<Focus> focusTarget(BlockLocation location, int caret) const;
    void focusEnd(PageView *page);
    void closeEditorsOfRemovedBlocks();
    void scheduleLayout();
    void updateHandle(const QPointF &point);
    std::optional<std::pair<PageView *, QWidget *>> blockViewAt(const QPointF &point) const;
    bool handleSlashMenuKey(QKeyEvent *event);
    void ensureVisible(QWidget *widget, const QRectF &rect);
    void pushTyping(BlockID id, const TextEdit &edit);

    BlueprintPalette m_palette;
    Note m_note;
    EditorDocument m_document;
    PageLayoutMode m_layoutMode = PageLayoutMode::slides;
    bool m_showsMarkdownSyntax = true;
    QScrollArea *m_scroll = nullptr;
    EditorDocumentView *m_documentView = nullptr;
    BlockHandleView *m_handle = nullptr;
    SlashMenuView *m_slashMenuView = nullptr;
    std::optional<SlashMenu> m_slashMenu;
    /// A `/` just typed at this place opens the menu once the edit lands.
    std::optional<std::pair<BlockID, int>> m_pendingSlash;
    DrawingSourceEditor *m_drawingEditor = nullptr;
    DeckEditor *m_deckEditor = nullptr;
    EditorToolbar *m_commandActions = nullptr;
    QUndoStack *m_undoStack = nullptr;
    ChangeCoalescer *m_changes = nullptr;
    QList<PageView *> m_pageViews;
    QHash<PageID, PageView *> m_pageViewsByID;
    /// Block views by id. Views of removed blocks are kept so an undo that
    /// brings a block back also brings back its view.
    QHash<BlockID, BlockTextView *> m_textViews;
    QHash<BlockID, DrawingBlockView *> m_drawingViews;
    QHash<BlockID, DeckBlockView *> m_deckViews;
    int m_visiblePage = 1;
    double m_fontSize = PageGeometry::bodyFontSize;
    bool m_isLayingOut = false;
    bool m_needsLayout = false;
    bool m_presentsEditors = true;
    int m_undoGeneration = 0;
    QString m_pendingActionName;

    friend class TypingCommand;
};

} // namespace wp
