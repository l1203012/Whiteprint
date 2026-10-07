#include "editor/NoteEditorView.h"

#include "editor/MarkdownEditing.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPalette>
#include <QScrollArea>
#include <QScrollBar>
#include <QUndoCommand>
#include <algorithm>
#include <cmath>

namespace wp {

// MARK: Undo commands

/// A text edit in one block (typing, deleting, pasting, or a Markdown shortcut).
class TypingCommand : public QUndoCommand
{
public:
    TypingCommand(NoteEditorView *editor, BlockID block, const TextEdit &edit, int generation)
        : m_editor(editor), m_block(block), m_edit(edit), m_generation(generation)
    {
    }

    int id() const override { return m_block.rawValue + 1; }

    bool mergeWith(const QUndoCommand *other) override
    {
        const auto *o = dynamic_cast<const TypingCommand *>(other);
        if (!o || o->m_generation != m_generation || !(o->m_block == m_block))
            return false;
        TextEdit &a = m_edit;
        const TextEdit &b = o->m_edit;
        if (a.removed.isEmpty() && b.removed.isEmpty() && b.position == a.position + a.added.size()) {
            a.added += b.added;
            return true;
        }
        if (a.added.isEmpty() && b.added.isEmpty()) {
            if (b.position + b.removed.size() == a.position) { // backspace
                a.position = b.position;
                a.removed = b.removed + a.removed;
                return true;
            }
            if (b.position == a.position) { // forward delete
                a.removed += b.removed;
                return true;
            }
        }
        return false;
    }

    void undo() override
    {
        m_editor->applyTextEdit(m_block, TextRange(m_edit.position, int(m_edit.added.size())), m_edit.removed);
    }

    void redo() override
    {
        if (m_first) {
            m_first = false;
            return;
        }
        m_editor->applyTextEdit(m_block, TextRange(m_edit.position, int(m_edit.removed.size())), m_edit.added);
    }

private:
    NoteEditorView *m_editor;
    BlockID m_block;
    TextEdit m_edit;
    int m_generation;
    bool m_first = true;
};

/// A block-level edit, undone by restoring the document as it was.
class SnapshotCommand : public QUndoCommand
{
public:
    SnapshotCommand(NoteEditorView *editor, NoteEditorView::Snapshot before, const QString &name)
        : QUndoCommand(name), m_editor(editor), m_snapshot(std::move(before))
    {
    }

    void undo() override { swap(); }
    void redo() override
    {
        if (m_first) {
            m_first = false;
            return;
        }
        swap();
    }

private:
    /// Restores the stored snapshot and keeps the state it replaces for the way back.
    void swap()
    {
        NoteEditorView::Snapshot current = m_editor->snapshotNow();
        m_editor->restore(m_snapshot);
        m_snapshot = std::move(current);
    }

    NoteEditorView *m_editor;
    NoteEditorView::Snapshot m_snapshot;
    bool m_first = true;
};

// MARK: Construction

NoteEditorView::NoteEditorView(const Note &note, const BlueprintPalette &palette, QWidget *parent)
    : QWidget(parent), m_palette(palette), m_document(note)
{
    m_note = m_document.note();
    m_undoStack = new QUndoStack(this);
    m_changes = new ChangeCoalescer(300, this);
    setUpViews();
    syncViews();
    qApp->installEventFilter(this);
}

NoteEditorView::~NoteEditorView()
{
    qApp->removeEventFilter(this);
    // Views of removed blocks have no parent; the rest belong to their pages.
    for (BlockTextView *view : std::as_const(m_textViews)) {
        if (!view->parentWidget())
            delete view;
    }
    for (DrawingBlockView *view : std::as_const(m_drawingViews)) {
        if (!view->parentWidget())
            delete view;
    }
    for (DeckBlockView *view : std::as_const(m_deckViews)) {
        if (!view->parentWidget())
            delete view;
    }
}

void NoteEditorView::setUpViews()
{
    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(false);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->viewport()->setAutoFillBackground(true);
    applyCanvas();
    m_documentView = new EditorDocumentView;
    m_scroll->setWidget(m_documentView);
    m_documentView->setAutoFillBackground(false);
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { didScroll(); });

    m_slashMenuView = new SlashMenuView(this);
    m_slashMenuView->hide();
    m_slashMenuView->onChoose = [this](int row) { chooseSlashCommand(row); };

    m_handle = new BlockHandleView(m_palette, m_documentView);
    m_handle->onClick = [this](BlockHandleView *handle) { showBlockMenu(handle); };
    m_documentView->onMouseExited = [this] { m_handle->hideHandle(); };
    m_documentView->onMouseMoved = [this](QPointF point) { updateHandle(point); };
}

void NoteEditorView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_scroll->setGeometry(rect());
    m_needsLayout = false;
    layoutDocument();
    realizeVisiblePages();
    positionSlashMenu();
}

void NoteEditorView::scheduleLayout()
{
    if (m_needsLayout)
        return;
    m_needsLayout = true;
    QMetaObject::invokeMethod(this, [this] { layoutSubtreeIfNeeded(); }, Qt::QueuedConnection);
}

void NoteEditorView::layoutSubtreeIfNeeded()
{
    if (!m_needsLayout)
        return;
    m_needsLayout = false;
    if (m_scroll->geometry() != rect())
        m_scroll->setGeometry(rect());
    layoutDocument();
    realizeVisiblePages();
    positionSlashMenu();
}

void NoteEditorView::setLayoutMode(PageLayoutMode mode)
{
    if (mode == m_layoutMode)
        return;
    m_layoutMode = mode;
    keepingTopBlockInPlace([this] {
        m_needsLayout = true;
        layoutSubtreeIfNeeded();
    });
}

void NoteEditorView::setBlueprintPalette(const BlueprintPalette &palette)
{
    if (palette == m_palette)
        return;
    m_palette = palette;
    applyCanvas();
    m_handle->setBlueprintPalette(palette);
    for (PageView *page : std::as_const(m_pageViews))
        page->setBlueprintPalette(palette);
    for (BlockTextView *view : std::as_const(m_textViews))
        view->setBlueprintPalette(palette);
    for (DrawingBlockView *view : std::as_const(m_drawingViews))
        view->setBlueprintPalette(palette);
    for (DeckBlockView *view : std::as_const(m_deckViews))
        view->setBlueprintPalette(palette);
    for (PageView *page : std::as_const(m_pageViews))
        page->estimatedWidth = 0;
    scheduleLayout();
}

/// The scroll background: the palette's canvas, or the window background when it has none.
void NoteEditorView::applyCanvas()
{
    QWidget *viewport = m_scroll->viewport();
    if (!m_palette.canvas.isValid()) {
        viewport->setPalette(QPalette());
        return;
    }
    QPalette p = viewport->palette();
    p.setColor(QPalette::Window, m_palette.canvas);
    p.setColor(QPalette::Base, m_palette.canvas);
    viewport->setPalette(p);
}

void NoteEditorView::setShowsMarkdownSyntax(bool shows)
{
    if (shows == m_showsMarkdownSyntax)
        return;
    m_showsMarkdownSyntax = shows;
    for (BlockTextView *view : std::as_const(m_textViews))
        view->setConcealsMarkup(!shows);
    scheduleLayout();
}

EditorToolbar *NoteEditorView::commandActions()
{
    if (!m_commandActions)
        m_commandActions = new EditorToolbar([this](EditorCommand command) { perform(command); }, this);
    return m_commandActions;
}

// MARK: Public API

void NoteEditorView::setNote(const Note &note, bool preservingSelection)
{
    if (note == m_note)
        return;
    closeSlashMenu();
    const auto focusBefore = currentFocus();
    const auto anchor = scrollAnchor();
    const Snapshot before{m_document, focusBefore};
    const EditorDocument old = m_document;
    m_document = m_document.reconciled(note);
    m_note = m_document.note();
    syncViews();
    closeEditorsOfRemovedBlocks();
    if (preservingSelection) {
        restoreScroll(anchor);
        if (focusBefore)
            focus(transferred(*focusBefore, old), false);
    } else {
        if (focusBefore) {
            if (QWidget *w = window()->focusWidget())
                w->clearFocus();
        }
        scrollToY(0);
    }
    registerUndo(before, QStringLiteral("External Edit"));
    updateVisiblePage();
}

void NoteEditorView::scrollToPage(int page)
{
    if (m_pageViews.isEmpty())
        return;
    const int index = std::min(std::max(page - 1, 0), int(m_pageViews.size()) - 1);
    m_needsLayout = true;
    layoutSubtreeIfNeeded();
    realize(index);
    scrollToY(m_pageViews[index]->y() + PageGeometry::shadowInset - PageGeometry::pageGap);
    realizeVisiblePages();
    // Realizing pages above can shift this one; settle on it once more.
    scrollToY(m_pageViews[index]->y() + PageGeometry::shadowInset - PageGeometry::pageGap);
    updateVisiblePage();
}

void NoteEditorView::insertDrawingAtSelection(const QString &source)
{
    const auto f = currentFocus();
    if (f && f->kind == Focus::Kind::text)
        insertDrawing(source, f->id, f->range.location, currentPageIndex());
    else
        insertDrawing(source, std::nullopt, std::nullopt, currentPageIndex());
}

void NoteEditorView::insertDeckAtSelection()
{
    const auto f = currentFocus();
    if (f && f->kind == Focus::Kind::text)
        insertDeck(f->id, f->range.location, currentPageIndex());
    else
        insertDeck(std::nullopt, std::nullopt, currentPageIndex());
}

void NoteEditorView::addPage()
{
    addPageAfter(currentPageIndex());
}

void NoteEditorView::perform(EditorCommand command)
{
    switch (command) {
    case EditorCommand::drawing: insertDrawingAtSelection(); return;
    case EditorCommand::flashcards: insertDeckAtSelection(); return;
    case EditorCommand::newPage: addPage(); return;
    default: break;
    }
    const auto f = currentFocus();
    if (!f || f->kind != Focus::Kind::text || !m_textViews.contains(f->id))
        return;
    BlockTextView *view = m_textViews.value(f->id);
    const auto change = MarkdownFormatting::apply(command, view->string(), view->selectedRange());
    if (!change)
        return;
    closeSlashMenu();
    perform(*change, view, EditorCommands::title(command));
}

void NoteEditorView::undo()
{
    m_undoStack->undo();
}

void NoteEditorView::redo()
{
    m_undoStack->redo();
}

void NoteEditorView::flushPendingChange()
{
    m_changes->flush();
}

// MARK: Layout

PageGeometry NoteEditorView::geometry() const
{
    return PageGeometry(m_scroll->viewport()->width(), m_layoutMode);
}

double NoteEditorView::scrollTop() const
{
    return m_scroll->verticalScrollBar()->value();
}

void NoteEditorView::scrollToY(double y)
{
    QScrollBar *bar = m_scroll->verticalScrollBar();
    const double maxY = std::max(0.0, double(m_documentView->height() - m_scroll->viewport()->height()));
    bar->setRange(0, int(maxY));
    bar->setValue(int(std::min(std::max(0.0, y), maxY)));
}

void NoteEditorView::layoutDocument()
{
    if (m_isLayingOut)
        return;
    m_isLayingOut = true;
    struct Reset {
        bool &flag;
        ~Reset() { flag = false; }
    } reset{m_isLayingOut};
    const int width = m_scroll->viewport()->width();
    if (width <= 0)
        return;
    const PageGeometry geo = geometry();
    const auto anchor = scrollAnchor();
    applyFontSize(geo.fontSize);
    const double inset = PageGeometry::shadowInset;
    double sheetTop = PageGeometry::pageGap;
    for (int index = 0; index < m_pageViews.size(); ++index) {
        PageView *page = m_pageViews[index];
        if (!page->isRealized && page->estimatedWidth != geo.textWidth()) {
            page->estimatedContentHeight = estimatedHeight(m_document.pages()[index], geo);
            page->estimatedWidth = geo.textWidth();
        }
        const double height = page->layoutBlocks(geo);
        const QRect frame(int(geo.pageX - inset), int(sheetTop - inset), int(geo.pageWidth + 2 * inset), int(height + 2 * inset));
        if (page->geometry() != frame)
            page->setGeometry(frame);
        page->setFollowsAnotherPage(index > 0);
        sheetTop += height + PageGeometry::pageGap;
    }
    const int height = int(std::max(sheetTop + PageGeometry::pageGap, double(m_scroll->viewport()->height())));
    if (m_documentView->size() != QSize(width, height))
        m_documentView->resize(width, height);
    QScrollBar *bar = m_scroll->verticalScrollBar();
    bar->setRange(0, std::max(0, height - m_scroll->viewport()->height()));
    bar->setPageStep(m_scroll->viewport()->height());
    restoreScroll(anchor);
    m_handle->hideHandle();
}

void NoteEditorView::applyFontSize(double size)
{
    if (size == m_fontSize)
        return;
    m_fontSize = size;
    for (BlockTextView *view : std::as_const(m_textViews))
        view->setFontSize(size);
    for (DeckBlockView *view : std::as_const(m_deckViews))
        view->setFontSize(size);
    for (PageView *page : std::as_const(m_pageViews))
        page->estimatedWidth = 0;
}

double NoteEditorView::estimatedHeight(const EditorPage &page, const PageGeometry &geo) const
{
    const double width = geo.textWidth();
    double total = 0;
    for (const EditorBlock &block : page.blocks) {
        switch (block.kind) {
        case BlockKind::text: total += HeightEstimate::text(block.textValue, width, geo.fontSize); break;
        case BlockKind::drawing:
            total += DrawingBlockView::heightFor(DrawingBlockView::canvasSize(block.drawingValue.source), width);
            break;
        case BlockKind::cards: total += DeckBlockView::estimatedHeight(block.deckValue, width, geo.fontSize); break;
        }
    }
    return total + std::max(0, int(page.blocks.size()) - 1) * geo.blockSpacing;
}

std::optional<NoteEditorView::ScrollAnchor> NoteEditorView::scrollAnchor() const
{
    const double top = scrollTop();
    if (top <= 0)
        return std::nullopt;
    for (int i = 0; i < m_pageViews.size(); ++i) {
        if (m_pageViews[i]->geometry().bottom() + 1 > top)
            return ScrollAnchor{i, top - m_pageViews[i]->y()};
    }
    return std::nullopt;
}

void NoteEditorView::restoreScroll(const std::optional<ScrollAnchor> &anchor)
{
    if (!anchor || anchor->page < 0 || anchor->page >= m_pageViews.size())
        return;
    const double y = m_pageViews[anchor->page]->y() + anchor->offset;
    if (std::abs(y - scrollTop()) > 0.5)
        scrollToY(y);
}

std::optional<std::pair<BlockID, double>> NoteEditorView::topBlock() const
{
    const double top = scrollTop();
    if (top <= 0)
        return std::nullopt;
    PageView *page = nullptr;
    for (PageView *p : m_pageViews) {
        if (p->geometry().bottom() + 1 > top) {
            page = p;
            break;
        }
    }
    if (!page || !page->isRealized || page->blockViews().isEmpty())
        return std::nullopt;
    const double local = top - page->y();
    QWidget *view = page->blockViews().last();
    for (QWidget *v : page->blockViews()) {
        if (v->geometry().bottom() + 1 > local) {
            view = v;
            break;
        }
    }
    const auto id = blockIDOf(view);
    if (!id)
        return std::nullopt;
    return std::make_pair(*id, std::min(1.0, std::max(0.0, (local - view->y()) / std::max<double>(view->height(), 1))));
}

void NoteEditorView::keepingTopBlockInPlace(const std::function<void()> &change)
{
    const auto anchor = topBlock();
    change();
    if (!anchor)
        return;
    for (int i = 0; i < 2; ++i) {
        QWidget *view = viewFor(anchor->first);
        if (!view || !view->parentWidget())
            return;
        m_needsLayout = true;
        layoutSubtreeIfNeeded();
        const double y = view->mapTo(m_documentView, QPoint(0, 0)).y();
        scrollToY(y + anchor->second * view->height());
        realizeVisiblePages();
    }
    updateVisiblePage();
}

void NoteEditorView::didScroll()
{
    m_handle->hideHandle();
    realizeVisiblePages();
    positionSlashMenu();
    updateVisiblePage();
}

void NoteEditorView::updateVisiblePage()
{
    const double top = scrollTop() + PageGeometry::pageGap;
    int index = std::max(0, int(m_pageViews.size()) - 1);
    for (int i = 0; i < m_pageViews.size(); ++i) {
        if (m_pageViews[i]->geometry().bottom() + 1 - PageGeometry::shadowInset > top) {
            index = i;
            break;
        }
    }
    if (index + 1 != m_visiblePage) {
        m_visiblePage = index + 1;
        if (onVisiblePageChange)
            onVisiblePageChange(m_visiblePage);
        emit visiblePageChanged(m_visiblePage);
    }
}

// MARK: Model to views

void NoteEditorView::syncViews()
{
    QList<PageView *> views;
    const QList<EditorPage> &pages = m_document.pages();
    for (const EditorPage &page : pages) {
        PageView *view = m_pageViewsByID.value(page.id);
        if (!view)
            view = makePageView(page.id);
        if (view->isRealized) {
            QList<QWidget *> blockViews;
            for (const EditorBlock &block : page.blocks)
                blockViews.append(blockViewFor(block, page.blocks.size() == 1));
            view->setBlockViews(blockViews);
        } else {
            view->estimatedWidth = 0;
        }
        views.append(view);
    }
    for (PageView *old : std::as_const(m_pageViews)) {
        if (!views.contains(old)) {
            old->setBlockViews({});
            old->hide();
            old->setParent(nullptr);
            m_pageViewsByID.remove(old->pageID());
            old->deleteLater();
        }
    }
    for (PageView *view : views) {
        if (view->parentWidget() != m_documentView) {
            view->setParent(m_documentView);
            view->show();
            m_handle->raise();
        }
    }
    m_pageViews = views;
    layoutDocument();
    realizeVisiblePages();
}

PageView *NoteEditorView::makePageView(PageID id)
{
    auto *view = new PageView(id, m_palette);
    view->setMouseTracking(true);
    view->onClickBelowBlocks = [this](PageView *page) { focusEnd(page); };
    m_pageViewsByID.insert(id, view);
    return view;
}

QWidget *NoteEditorView::blockViewFor(const EditorBlock &block, bool alone)
{
    switch (block.kind) {
    case BlockKind::text: {
        BlockTextView *view = m_textViews.value(block.id);
        if (!view)
            view = makeTextView(block.id, block.textValue);
        view->setText(block.textValue);
        view->setAlonePlaceholder(alone);
        return view;
    }
    case BlockKind::drawing: {
        DrawingBlockView *view = m_drawingViews.value(block.id);
        if (!view)
            view = makeDrawingView(block.id, block.drawingValue.source);
        view->setSource(block.drawingValue.source);
        view->update();
        return view;
    }
    case BlockKind::cards: {
        DeckBlockView *view = m_deckViews.value(block.id);
        if (!view)
            view = makeDeckView(block.id, block.deckValue);
        view->setDeck(block.deckValue);
        return view;
    }
    }
    return nullptr;
}

BlockTextView *NoteEditorView::makeTextView(BlockID id, const QString &text)
{
    auto *view = new BlockTextView(id, text, m_palette, std::max(geometry().textWidth(), 100.0), m_fontSize, !m_showsMarkdownSyntax);
    view->setDelegate(this);
    view->setMouseTracking(true);
    view->viewport()->setMouseTracking(true);
    m_textViews.insert(id, view);
    return view;
}

DrawingBlockView *NoteEditorView::makeDrawingView(BlockID id, const QString &source)
{
    auto *view = new DrawingBlockView(id, source, m_palette);
    view->setDelegate(this);
    m_drawingViews.insert(id, view);
    return view;
}

DeckBlockView *NoteEditorView::makeDeckView(BlockID id, const CardDeck &deck)
{
    auto *view = new DeckBlockView(id, deck, m_palette, m_fontSize);
    view->setDelegate(this);
    m_deckViews.insert(id, view);
    return view;
}

void NoteEditorView::realize(int index)
{
    if (index < 0 || index >= m_pageViews.size() || m_pageViews[index]->isRealized)
        return;
    const QList<EditorBlock> &blocks = m_document.pages()[index].blocks;
    m_pageViews[index]->isRealized = true;
    QList<QWidget *> views;
    for (const EditorBlock &block : blocks)
        views.append(blockViewFor(block, blocks.size() == 1));
    m_pageViews[index]->setBlockViews(views);
    layoutDocument();
}

QRectF NoteEditorView::nearbyRect() const
{
    const double visibleHeight = m_scroll->viewport()->height();
    const double margin = std::max(visibleHeight, 400.0);
    return QRectF(0, scrollTop() - margin, m_scroll->viewport()->width(), visibleHeight + 2 * margin);
}

void NoteEditorView::realizeVisiblePages()
{
    if (m_scroll->viewport()->width() <= 0)
        return;
    for (int pass = 0; pass < 4; ++pass) {
        const QRectF range = nearbyRect();
        QList<int> pending;
        for (int i = 0; i < m_pageViews.size(); ++i) {
            if (!m_pageViews[i]->isRealized && QRectF(m_pageViews[i]->geometry()).intersects(range))
                pending.append(i);
        }
        if (pending.isEmpty())
            return;
        for (int index : pending)
            realize(index);
    }
}

QWidget *NoteEditorView::viewFor(BlockID id)
{
    const auto location = m_document.location(id);
    if (!location)
        return nullptr;
    if (!m_pageViews[location->page]->isRealized)
        realize(location->page);
    if (QWidget *w = m_textViews.value(id))
        return w;
    if (QWidget *w = m_drawingViews.value(id))
        return w;
    return m_deckViews.value(id);
}

std::optional<BlockID> NoteEditorView::blockIDOf(QWidget *view) const
{
    if (auto *t = qobject_cast<BlockTextView *>(view))
        return t->blockID();
    if (auto *d = qobject_cast<DrawingBlockView *>(view))
        return d->blockID();
    if (auto *c = qobject_cast<DeckBlockView *>(view))
        return c->blockID();
    return std::nullopt;
}

// MARK: Changes

void NoteEditorView::noteDidChange()
{
    m_note = m_document.note();
    m_changes->schedule([this] {
        if (onChange)
            onChange(m_note);
        emit changed(m_note);
    });
}

void NoteEditorView::blockTextViewDidChangeHeight(BlockTextView *)
{
    if (!m_isLayingOut)
        scheduleLayout();
}

// MARK: Focus

std::optional<NoteEditorView::Focus> NoteEditorView::currentFocus() const
{
    QWidget *w = window() ? window()->focusWidget() : nullptr;
    if (!w)
        return std::nullopt;
    if (auto *text = qobject_cast<BlockTextView *>(w)) {
        if (m_textViews.value(text->blockID()) == text && m_document.block(text->blockID()))
            return Focus::text(text->blockID(), text->selectedRange());
        return std::nullopt;
    }
    // A text view's viewport can hold the focus widget too.
    if (auto id = blockIDOf(w)) {
        if (m_document.block(*id))
            return Focus::block(*id);
    }
    return std::nullopt;
}

void NoteEditorView::ensureVisible(QWidget *widget, const QRectF &rect)
{
    const QPoint topLeft = widget->mapTo(m_documentView, rect.topLeft().toPoint());
    m_scroll->ensureVisible(topLeft.x(), topLeft.y() + int(rect.height()), 0, 40);
    m_scroll->ensureVisible(topLeft.x(), topLeft.y(), 0, 40);
}

void NoteEditorView::focus(const std::optional<Focus> &target, bool scroll)
{
    if (!target)
        return;
    if (target->kind == Focus::Kind::text) {
        auto *view = qobject_cast<BlockTextView *>(viewFor(target->id));
        if (!view)
            return;
        m_needsLayout = true;
        layoutSubtreeIfNeeded();
        view->setFocus(Qt::OtherFocusReason);
        view->setSelectedRange(target->range);
        if (scroll)
            ensureVisible(view, view->lineRect(view->selectedRange().end()));
    } else {
        QWidget *view = viewFor(target->id);
        if (!view || qobject_cast<BlockTextView *>(view))
            return;
        m_needsLayout = true;
        layoutSubtreeIfNeeded();
        view->setFocus(Qt::OtherFocusReason);
        if (scroll)
            ensureVisible(view, view->rect());
    }
}

std::optional<NoteEditorView::Focus> NoteEditorView::transferred(const Focus &f, const EditorDocument &old) const
{
    if (f.kind == Focus::Kind::text) {
        const auto now = m_document.block(f.id);
        const auto previous = old.block(f.id);
        if (now && now->isText() && previous && previous->isText())
            return Focus::text(f.id, CaretTransform::transform(f.range, previous->textValue, now->textValue));
        if (const auto location = old.location(f.id))
            return focusTarget(*location, f.range.location);
        return std::nullopt;
    }
    if (m_document.block(f.id))
        return Focus::block(f.id);
    if (const auto location = old.location(f.id))
        return focusTarget(*location, 0);
    return std::nullopt;
}

std::optional<NoteEditorView::Focus> NoteEditorView::focusTarget(BlockLocation location, int caret) const
{
    const auto &pages = m_document.pages();
    if (pages.isEmpty())
        return std::nullopt;
    const int page = std::min(location.page, int(pages.size()) - 1);
    const auto &blocks = pages[page].blocks;
    const EditorBlock &block = blocks[std::min(location.block, int(blocks.size()) - 1)];
    if (block.isText())
        return Focus::text(block.id, TextRange(std::min(caret, int(block.textValue.size())), 0));
    return Focus::block(block.id);
}

int NoteEditorView::currentPageIndex() const
{
    if (const auto f = currentFocus()) {
        if (const auto location = m_document.location(f->id))
            return location->page;
    }
    return std::min(m_visiblePage - 1, int(m_document.pages().size()) - 1);
}

void NoteEditorView::focusEnd(PageView *page)
{
    const int index = int(m_pageViews.indexOf(page));
    if (index < 0 || m_document.pages()[index].blocks.isEmpty())
        return;
    focus(endFocus(m_document.pages()[index].blocks.last()));
}

NoteEditorView::Focus NoteEditorView::endFocus(const EditorBlock &block) const
{
    if (block.isText())
        return Focus::text(block.id, TextRange(int(block.textValue.size()), 0));
    return Focus::block(block.id);
}

// MARK: Undo

std::optional<NoteEditorView::Focus> NoteEditorView::performBlockEdit(const QString &name,
                                                                     const std::function<std::optional<Focus>(EditorDocument &)> &body)
{
    closeSlashMenu();
    const Snapshot before{m_document, currentFocus()};
    EditorDocument edited = m_document;
    const std::optional<Focus> after = body(edited);
    if (edited == m_document)
        return std::nullopt;
    m_document = edited;
    syncViews();
    noteDidChange();
    focus(after);
    registerUndo(before, name);
    // Callers distinguish "edited" from "unchanged"; a body returning no focus still edited.
    return after ? after : std::optional<Focus>(Focus::block(BlockID{0}));
}

void NoteEditorView::registerUndo(const Snapshot &before, const QString &name)
{
    ++m_undoGeneration;
    m_undoStack->push(new SnapshotCommand(this, before, name));
}

void NoteEditorView::restore(const Snapshot &snapshot)
{
    ++m_undoGeneration;
    closeSlashMenu();
    m_document = m_document.restoring(snapshot.document);
    closeEditorsOfRemovedBlocks();
    syncViews();
    noteDidChange();
    focus(snapshot.focus);
}

void NoteEditorView::closeEditorsOfRemovedBlocks()
{
    if (m_drawingEditor) {
        const auto block = m_document.block(m_drawingEditor->blockID());
        if (!block || block->kind != BlockKind::drawing)
            closeDrawingEditor(false);
    }
    if (m_deckEditor) {
        const auto block = m_document.block(m_deckEditor->blockID());
        if (!block || block->kind != BlockKind::cards)
            closeDeckEditor(false);
    }
}

void NoteEditorView::pushTyping(BlockID id, const TextEdit &edit)
{
    auto *command = new TypingCommand(this, id, edit, m_undoGeneration);
    command->setText(m_pendingActionName.isEmpty() ? QStringLiteral("Typing") : m_pendingActionName);
    m_undoStack->push(command);
}

void NoteEditorView::applyTextEdit(BlockID block, TextRange range, const QString &replacement)
{
    BlockTextView *view = m_textViews.value(block);
    if (!view)
        return;
    ++m_undoGeneration;
    closeSlashMenu();
    view->replaceSilently(range, replacement);
    m_document.setText(view->string(), block);
    noteDidChange();
    focus(Focus::text(block, TextRange(range.location + int(replacement.size()), 0)));
}

// MARK: Events

bool NoteEditorView::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize && watched == m_scroll->viewport()) {
        m_needsLayout = false;
        layoutDocument();
        realizeVisiblePages();
        positionSlashMenu();
    } else if (event->type() == QEvent::MouseMove) {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && (widget == m_documentView || m_documentView->isAncestorOf(widget))) {
            const QPoint global = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
            updateHandle(m_documentView->mapFromGlobal(global));
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace wp
