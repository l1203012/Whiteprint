// Keyboard, mouse and menu behaviours. The decisions are made by the pure
// model (`MarkdownEditing`, `EditorDocument`, `SlashMenuState`); this file
// routes events to them and applies the results.
#include "editor/NoteEditorView.h"

#include "editor/MarkdownEditing.h"

#include <QAction>
#include <QKeyEvent>
#include <QMenu>
#include <QScrollArea>
#include <algorithm>
#include <cmath>

namespace wp {

void NoteEditorView::perform(const TextChange &change, BlockTextView *view, const QString &actionName)
{
    ++m_undoGeneration;
    m_pendingActionName = actionName;
    view->replaceText(change.range, change.replacement);
    m_pendingActionName.clear();
    view->setSelectedRange(change.selection);
    ++m_undoGeneration;
}

bool NoteEditorView::blockTextViewWillInsert(BlockTextView *view, const QString &string, TextRange range)
{
    if (const auto change = MarkdownEditing::shortcut(string, range, view->string())) {
        perform(*change, view);
        return true;
    }
    if (string == QLatin1String("/") && MarkdownEditing::opensSlashMenu(range.location, view->string()))
        m_pendingSlash = std::make_pair(view->blockID(), range.location);
    return false;
}

void NoteEditorView::blockTextViewDidEdit(BlockTextView *view, const TextEdit &edit)
{
    if (m_textViews.value(view->blockID()) != view)
        return;
    pushTyping(view->blockID(), edit);
    m_document.setText(view->string(), view->blockID());
    noteDidChange();
    if (m_pendingSlash && m_pendingSlash->first == view->blockID()) {
        const int location = m_pendingSlash->second;
        m_pendingSlash.reset();
        openSlashMenu(view, location);
    } else {
        updateSlashMenu(view);
    }
}

void NoteEditorView::blockTextViewDidChangeSelection(BlockTextView *view)
{
    updateSlashMenu(view);
}

void NoteEditorView::blockTextViewUndoRequested(BlockTextView *, bool isRedo)
{
    if (isRedo)
        redo();
    else
        undo();
}

bool NoteEditorView::blockTextViewHandleKey(BlockTextView *view, QKeyEvent *event)
{
    if (m_slashMenu && m_slashMenu->block == view->blockID() && handleSlashMenuKey(event))
        return true;
    const QString text = view->string();
    const TextRange selection = view->selectedRange();
    const int length = int(text.size());
    const auto mods = event->modifiers() & ~Qt::KeypadModifier;
    const bool plain = mods == Qt::NoModifier;
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        if (!plain)
            return false;
        const auto change = MarkdownEditing::newline(text, selection);
        if (!change)
            return false;
        perform(*change, view);
        return true;
    }
    case Qt::Key_Tab:
    case Qt::Key_Backtab: {
        const bool outdent = event->key() == Qt::Key_Backtab || (mods & Qt::ShiftModifier);
        if (!plain && !(mods == Qt::ShiftModifier))
            return false;
        if (const auto change = MarkdownEditing::indent(text, selection, outdent)) {
            perform(*change, view, outdent ? QStringLiteral("Outdent") : QStringLiteral("Indent"));
            return true;
        }
        return outdent;
    }
    case Qt::Key_Up:
        return plain && selection.length == 0 && view->caretIsOnFirstLine() && moveFocus(view->blockID(), false, view->caretX());
    case Qt::Key_Down:
        return plain && selection.length == 0 && view->caretIsOnLastLine() && moveFocus(view->blockID(), true, view->caretX());
    case Qt::Key_Left:
        return plain && selection == TextRange(0, 0) && moveFocus(view->blockID(), false, std::nullopt);
    case Qt::Key_Right:
        return plain && selection == TextRange(length, 0) && moveFocus(view->blockID(), true, std::nullopt);
    case Qt::Key_Backspace:
        return plain && selection == TextRange(0, 0) && backspaceAtStart(view->blockID());
    default:
        return false;
    }
}

void NoteEditorView::blockTextViewToggleCheckbox(BlockTextView *view, int location)
{
    const auto change = MarkdownEditing::toggleCheckbox(location, view->string(), view->selectedRange());
    if (!change)
        return;
    perform(*change, view, QStringLiteral("Toggle Checkbox"));
}

void NoteEditorView::blockTextViewDidBecomeFocused(BlockTextView *view)
{
    if (m_slashMenu && m_slashMenu->block != view->blockID())
        closeSlashMenu();
}

// MARK: Moving between blocks

bool NoteEditorView::moveFocus(BlockID from, bool forward, std::optional<double> x)
{
    const auto target = forward ? m_document.blockAfter(from) : m_document.blockBefore(from);
    if (!target)
        return false;
    if (!target->isText()) {
        focus(Focus::block(target->id));
        return true;
    }
    const int length = int(target->textValue.size());
    focus(Focus::text(target->id, TextRange(forward ? 0 : length, 0)));
    if (x) {
        if (BlockTextView *view = m_textViews.value(target->id)) {
            view->placeCaret(*x, forward);
            ensureVisible(view, view->lineRect(view->selectedRange().location));
        }
    }
    return true;
}

bool NoteEditorView::backspaceAtStart(BlockID id)
{
    const auto location = m_document.location(id);
    if (!location)
        return false;
    if (location->block > 0) {
        const EditorBlock previous = m_document.pages()[location->page].blocks[location->block - 1];
        if (!previous.isText()) {
            focus(Focus::block(previous.id));
            return true;
        }
        performBlockEdit(QStringLiteral("Join Blocks"), [id](EditorDocument &document) -> std::optional<Focus> {
            if (const auto merged = document.mergeWithPrevious(id))
                return Focus::text(merged->block, TextRange(merged->offset, 0));
            return std::nullopt;
        });
        return true;
    }
    if (location->page <= 0)
        return false;
    const EditorPage page = m_document.pages()[location->page];
    const int pageIndex = location->page;
    if (page.blocks.size() == 1 && page.blocks[0].isEmptyText()) {
        performBlockEdit(QStringLiteral("Delete Page"), [this, pageIndex](EditorDocument &document) -> std::optional<Focus> {
            document.removePage(pageIndex);
            const auto &blocks = document.pages()[pageIndex - 1].blocks;
            if (blocks.isEmpty())
                return std::nullopt;
            return endFocus(blocks.last());
        });
    } else {
        performBlockEdit(QStringLiteral("Join Pages"), [id, pageIndex](EditorDocument &document) -> std::optional<Focus> {
            document.mergePageWithPrevious(pageIndex);
            return Focus::text(id, TextRange(0, 0));
        });
    }
    return true;
}

// MARK: Slash menu

void NoteEditorView::openSlashMenu(BlockTextView *view, int location)
{
    SlashMenuState state(location);
    if (!state.update(view->string(), view->selectedRange().location))
        return;
    m_slashMenu = SlashMenu{view->blockID(), state};
    m_slashMenuView->setState(state);
    m_slashMenuView->show();
    positionSlashMenu();
}

void NoteEditorView::updateSlashMenu(BlockTextView *view)
{
    if (!m_slashMenu || m_slashMenu->block != view->blockID())
        return;
    const TextRange selection = view->selectedRange();
    if (selection.length != 0 || !m_slashMenu->state.update(view->string(), selection.location)) {
        closeSlashMenu();
        return;
    }
    m_slashMenuView->setState(m_slashMenu->state);
    positionSlashMenu();
}

void NoteEditorView::closeSlashMenu()
{
    m_pendingSlash.reset();
    if (!m_slashMenu)
        return;
    m_slashMenu.reset();
    m_slashMenuView->setState(std::nullopt);
    m_slashMenuView->hide();
}

bool NoteEditorView::handleSlashMenuKey(QKeyEvent *event)
{
    if (!m_slashMenu)
        return false;
    switch (event->key()) {
    case Qt::Key_Up: m_slashMenu->state.moveSelection(-1); break;
    case Qt::Key_Down: m_slashMenu->state.moveSelection(1); break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Tab: chooseSlashCommand(std::nullopt); return true;
    case Qt::Key_Escape: closeSlashMenu(); return true;
    default: return false;
    }
    m_slashMenuView->setState(m_slashMenu->state);
    return true;
}

void NoteEditorView::chooseSlashCommand(std::optional<int> row)
{
    if (!m_slashMenu)
        return;
    SlashMenu menu = *m_slashMenu;
    BlockTextView *view = m_textViews.value(menu.block);
    if (!view)
        return;
    if (row)
        menu.state.select(*row);
    closeSlashMenu();
    const auto command = menu.state.selected();
    if (!command)
        return;
    view->setFocus(Qt::OtherFocusReason);
    const auto effect = MarkdownEditing::apply(*command, view->string(), menu.state.typedRange());
    using Kind = MarkdownEditing::SlashEffect::Kind;
    switch (effect.kind) {
    case Kind::text: perform(effect.change, view, SlashCommands::title(*command)); break;
    case Kind::insertDrawing:
        perform(effect.change, view);
        insertDrawing(QString(), view->blockID(), effect.change.selection.location, currentPageIndex());
        break;
    case Kind::insertDeck:
        perform(effect.change, view);
        insertDeck(view->blockID(), effect.change.selection.location, currentPageIndex());
        break;
    case Kind::newPage: {
        perform(effect.change, view);
        const auto location = m_document.location(view->blockID());
        addPageAfter(location ? location->page : currentPageIndex());
        break;
    }
    }
}

void NoteEditorView::positionSlashMenu()
{
    if (!m_slashMenu) {
        m_slashMenuView->hide();
        return;
    }
    BlockTextView *view = m_textViews.value(m_slashMenu->block);
    if (!view || !view->parentWidget()) {
        m_slashMenuView->hide();
        return;
    }
    const QRectF local = view->rect(TextRange(m_slashMenu->state.slashLocation(), 1));
    const QPoint topLeft = view->mapTo(this, local.topLeft().toPoint());
    const QRect anchor(topLeft, local.size().toSize());
    const QSize size = m_slashMenuView->preferredSize();
    const int margin = SlashMenuView::shadowMargin;
    QPoint origin(anchor.left() - 6 - margin, anchor.bottom() + 6 - margin);
    if (origin.y() + size.height() - margin > height() - 8 && anchor.top() - 6 - size.height() + margin > 8)
        origin.setY(anchor.top() - 6 - size.height() + margin);
    origin.setX(std::max(8 - margin, std::min(origin.x(), width() - size.width() + margin - 8)));
    m_slashMenuView->setGeometry(QRect(origin, size));
    m_slashMenuView->show();
    m_slashMenuView->raise();
}

// MARK: Block operations

void NoteEditorView::insertDrawing(const QString &source, std::optional<BlockID> splitting, std::optional<int> offset, int page)
{
    const auto focusAfter = performBlockEdit(QStringLiteral("Insert Drawing"), [&](EditorDocument &document) -> std::optional<Focus> {
        if (splitting && offset) {
            if (const auto id = document.insertDrawing(source, *splitting, *offset))
                return Focus::block(*id);
        }
        return Focus::block(document.appendDrawing(source, page));
    });
    if (focusAfter && focusAfter->kind == Focus::Kind::block) {
        layoutSubtreeIfNeeded();
        editDrawing(focusAfter->id);
    }
}

void NoteEditorView::insertDeck(std::optional<BlockID> splitting, std::optional<int> offset, int page)
{
    const CardDeck empty{QList<Flashcard>{}};
    const auto focusAfter = performBlockEdit(QStringLiteral("Insert Flashcards"), [&](EditorDocument &document) -> std::optional<Focus> {
        if (splitting && offset) {
            if (const auto id = document.insertDeck(empty, *splitting, *offset))
                return Focus::block(*id);
        }
        return Focus::block(document.appendDeck(empty, page));
    });
    if (focusAfter && focusAfter->kind == Focus::Kind::block) {
        layoutSubtreeIfNeeded();
        editDeck(focusAfter->id);
    }
}

void NoteEditorView::addPageAfter(int page)
{
    const auto focusAfter = performBlockEdit(QStringLiteral("Add Page"), [page](EditorDocument &document) -> std::optional<Focus> {
        return Focus::text(document.insertPage(page), TextRange(0, 0));
    });
    if (focusAfter)
        scrollToPage(page + 2);
}

void NoteEditorView::deleteBlock(BlockID id)
{
    const auto block = m_document.block(id);
    QString name = QStringLiteral("Delete Block");
    if (block && block->kind == BlockKind::drawing)
        name = QStringLiteral("Delete Drawing");
    else if (block && block->kind == BlockKind::cards)
        name = QStringLiteral("Delete Flashcards");
    performBlockEdit(name, [this, id](EditorDocument &document) -> std::optional<Focus> {
        const auto before = document.blockBefore(id);
        const auto after = document.blockAfter(id);
        const auto fallback = document.removeBlock(id);
        if (before && before->isText() && document.block(before->id))
            return endFocus(*before);
        if (after && after->isText() && document.block(after->id))
            return Focus::text(after->id, TextRange(0, 0));
        if (fallback) {
            if (const auto b = document.block(*fallback))
                return endFocus(*b);
        }
        return std::nullopt;
    });
}

void NoteEditorView::duplicateBlock(BlockID id)
{
    performBlockEdit(QStringLiteral("Duplicate Block"), [id](EditorDocument &document) -> std::optional<Focus> {
        document.duplicateBlock(id);
        return std::nullopt;
    });
}

void NoteEditorView::moveBlock(BlockID id, int delta)
{
    const auto wasFocused = currentFocus();
    performBlockEdit(delta < 0 ? QStringLiteral("Move Block Up") : QStringLiteral("Move Block Down"),
                     [&](EditorDocument &document) -> std::optional<Focus> { return document.moveBlock(id, delta) ? wasFocused : std::nullopt; });
}

// MARK: Handle

std::optional<std::pair<PageView *, QWidget *>> NoteEditorView::blockViewAt(const QPointF &point) const
{
    for (PageView *page : m_pageViews) {
        if (!QRectF(page->geometry()).contains(point))
            continue;
        if (!page->isRealized)
            return std::nullopt;
        const QPointF local = point - page->pos();
        if (!page->sheetRect().contains(local))
            return std::nullopt;
        const double half = geometry().blockSpacing / 2;
        for (QWidget *view : page->blockViews()) {
            if (local.y() >= view->y() - half && local.y() < view->y() + view->height() + half)
                return std::make_pair(page, view);
        }
        return std::nullopt;
    }
    return std::nullopt;
}

void NoteEditorView::updateHandle(const QPointF &point)
{
    const auto found = blockViewAt(point);
    if (!found) {
        if (!QRectF(m_handle->geometry()).adjusted(-6, -6, 6, 6).contains(point))
            m_handle->hideHandle();
        return;
    }
    QWidget *view = found->second;
    BlockID id;
    double lineMid = 0;
    if (auto *text = qobject_cast<BlockTextView *>(view)) {
        if (text->string().isEmpty()) {
            m_handle->hideHandle();
            return;
        }
        id = text->blockID();
        lineMid = text->lineRect(0).center().y();
    } else if (auto *drawing = qobject_cast<DrawingBlockView *>(view)) {
        id = drawing->blockID();
        lineMid = DrawingBlockView::verticalPadding + 12;
    } else if (auto *deck = qobject_cast<DeckBlockView *>(view)) {
        id = deck->blockID();
        const auto metrics = DeckLayout::metrics(deck->fontSize());
        lineMid = metrics.padding + metrics.headerHeight / 2;
    } else {
        return;
    }
    const QPoint frameTopLeft = view->mapTo(m_documentView, QPoint(0, 0));
    const QSize size = BlockHandleView::handleSize();
    m_handle->showFor(id, QPoint(frameTopLeft.x() - size.width() - 10, int(std::round(frameTopLeft.y() + lineMid - size.height() / 2.0))));
}

void NoteEditorView::showBlockMenu(BlockHandleView *handle)
{
    const auto blockID = handle->blockID();
    if (!blockID)
        return;
    const BlockID id = *blockID;
    EditorDocument probe = m_document;
    const bool canMoveUp = probe.moveBlock(id, -1);
    probe = m_document;
    const bool canMoveDown = probe.moveBlock(id, 1);
    const auto block = m_document.block(id);

    QMenu menu(this);
    menu.addAction(QStringLiteral("Delete"), this, [this, id] { deleteBlock(id); });
    menu.addAction(QStringLiteral("Duplicate"), this, [this, id] { duplicateBlock(id); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Move Up"), this, [this, id] { moveBlock(id, -1); })->setEnabled(canMoveUp);
    menu.addAction(QStringLiteral("Move Down"), this, [this, id] { moveBlock(id, 1); })->setEnabled(canMoveDown);
    if (block && block->kind == BlockKind::drawing) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Edit Drawing…"), this, [this, id] { editDrawing(id); });
    }
    if (block && block->kind == BlockKind::cards) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Edit Flashcards…"), this, [this, id] { editDeck(id); });
        menu.addAction(QStringLiteral("Study"), this,
                       [this, id] {
                           if (const auto b = m_document.block(id)) {
                               if (onStudyDeck)
                                   onStudyDeck(b->deckValue);
                               emit studyDeckRequested(b->deckValue);
                           }
                       })
            ->setEnabled(!block->deckValue.cards.isEmpty());
    }
    menu.exec(handle->mapToGlobal(QPoint(0, handle->height() + 4)));
}

// MARK: Selected drawings and decks

bool NoteEditorView::handleKeyOnSelected(QKeyEvent *event, BlockID id)
{
    switch (event->key()) {
    case Qt::Key_Backspace:
    case Qt::Key_Delete: deleteBlock(id); break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        performBlockEdit(QStringLiteral("New Line"), [id](EditorDocument &document) -> std::optional<Focus> {
            if (const auto next = document.textBlockAfter(id))
                return Focus::text(*next, TextRange(0, 0));
            return std::nullopt;
        });
        if (const auto f = currentFocus(); f && f->kind == Focus::Kind::block) {
            if (const auto next = m_document.blockAfter(id); next && next->isText())
                focus(Focus::text(next->id, TextRange(0, 0)));
        }
        break;
    case Qt::Key_Space: {
        const auto block = m_document.block(id);
        if (block && block->kind == BlockKind::cards)
            editDeck(id);
        else
            editDrawing(id);
        break;
    }
    case Qt::Key_Left:
    case Qt::Key_Up: moveFocus(id, false, std::nullopt); break;
    case Qt::Key_Right:
    case Qt::Key_Down: moveFocus(id, true, std::nullopt); break;
    default: return false;
    }
    return true;
}

// MARK: Drawings

void NoteEditorView::drawingBlockViewRequestsEditor(DrawingBlockView *view)
{
    editDrawing(view->blockID());
}

bool NoteEditorView::drawingBlockViewHandleKey(DrawingBlockView *view, QKeyEvent *event)
{
    return handleKeyOnSelected(event, view->blockID());
}

void NoteEditorView::editDrawing(BlockID id)
{
    auto *view = qobject_cast<DrawingBlockView *>(viewFor(id));
    if (!view)
        return;
    closeDrawingEditor(true);
    closeDeckEditor(true);
    m_drawingEditor = new DrawingSourceEditor(id, view->source(), m_palette, [this, id](const QString &source) { commitDrawing(id, source); }, this);
    if (m_presentsEditors && isVisible())
        m_drawingEditor->present(view);
}

void NoteEditorView::closeDrawingEditor(bool commit)
{
    DrawingSourceEditor *editor = m_drawingEditor;
    if (!editor)
        return;
    m_drawingEditor = nullptr;
    if (commit)
        editor->close();
    else
        editor->discard();
    editor->deleteLater();
}

void NoteEditorView::commitDrawing(BlockID id, const QString &source)
{
    if (m_drawingEditor && m_drawingEditor->blockID() == id) {
        DrawingSourceEditor *editor = m_drawingEditor;
        m_drawingEditor = nullptr;
        editor->deleteLater();
    }
    const auto block = m_document.block(id);
    if (!block || block->kind != BlockKind::drawing || block->drawingValue.source == source)
        return;
    performBlockEdit(QStringLiteral("Edit Drawing"), [&](EditorDocument &document) -> std::optional<Focus> {
        document.updateDrawing(id, source);
        return std::nullopt;
    });
}

// MARK: Flashcard decks

void NoteEditorView::deckBlockViewRequestsEditor(DeckBlockView *view)
{
    editDeck(view->blockID());
}

void NoteEditorView::deckBlockViewRequestsStudy(DeckBlockView *view)
{
    if (const auto block = m_document.block(view->blockID())) {
        if (block->kind == BlockKind::cards) {
            if (onStudyDeck)
                onStudyDeck(block->deckValue);
            emit studyDeckRequested(block->deckValue);
        }
    }
}

bool NoteEditorView::deckBlockViewHandleKey(DeckBlockView *view, QKeyEvent *event)
{
    return handleKeyOnSelected(event, view->blockID());
}

void NoteEditorView::deckBlockViewDidChangeHeight(DeckBlockView *)
{
    scheduleLayout();
}

void NoteEditorView::editDeck(BlockID id)
{
    auto *view = qobject_cast<DeckBlockView *>(viewFor(id));
    const auto block = m_document.block(id);
    if (!view || !block || block->kind != BlockKind::cards)
        return;
    closeDrawingEditor(true);
    closeDeckEditor(true);
    m_deckEditor = new DeckEditor(id, block->deckValue, [this, id](const CardDeck &deck) { commitDeck(id, deck); }, this);
    if (m_presentsEditors && isVisible())
        m_deckEditor->present(view);
}

void NoteEditorView::closeDeckEditor(bool commit)
{
    DeckEditor *editor = m_deckEditor;
    if (!editor)
        return;
    m_deckEditor = nullptr;
    if (commit)
        editor->close();
    else
        editor->discard();
    editor->deleteLater();
}

void NoteEditorView::commitDeck(BlockID id, const CardDeck &deck)
{
    if (m_deckEditor && m_deckEditor->blockID() == id) {
        DeckEditor *editor = m_deckEditor;
        m_deckEditor = nullptr;
        editor->deleteLater();
    }
    const auto block = m_document.block(id);
    if (!block || block->kind != BlockKind::cards)
        return;
    const CardDeck &old = block->deckValue;
    if (old.title == deck.title && old.cards == deck.cards)
        return;
    performBlockEdit(QStringLiteral("Edit Flashcards"), [&](EditorDocument &document) -> std::optional<Focus> {
        document.updateDeck(id, deck);
        return std::nullopt;
    });
}

} // namespace wp
