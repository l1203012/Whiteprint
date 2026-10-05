#include "editor/EditorDocument.h"

#include "core/TextUtil.h"
#include "editor/MarkdownLine.h"

#include <QSet>
#include <algorithm>

namespace wp {

EditorDocument::EditorDocument(const Note &note) : EditorDocument(note, 0) {}

EditorDocument::EditorDocument(const Note &note, int lastID) : m_base(note), m_lastID(lastID)
{
    m_base.setPages({});
    for (const NotePage &page : note.pages()) {
        QList<EditorBlock> blocks;
        for (const NoteBlock &block : page.blocks)
            blocks.append(makeBlock(block));
        m_pages.append(EditorPage{makePageID(), blocks});
    }
    for (int p = 0; p < m_pages.size(); ++p)
        normalize(p);
}

Note EditorDocument::note() const
{
    Note result = m_base;
    QList<NotePage> pages;
    for (const EditorPage &page : m_pages) {
        NotePage notePage;
        for (const EditorBlock &block : page.blocks) {
            switch (block.kind) {
            case BlockKind::text: {
                const QString canonicalText = canonical(block.textValue);
                if (!canonicalText.isEmpty())
                    notePage.blocks.append(NoteBlock::textBlock(canonicalText));
                break;
            }
            case BlockKind::drawing: notePage.blocks.append(NoteBlock::drawingBlock(block.drawingValue)); break;
            case BlockKind::cards: notePage.blocks.append(NoteBlock::cardsBlock(block.deckValue)); break;
            }
        }
        pages.append(notePage);
    }
    result.setPages(pages);
    return result;
}

// MARK: Lookup

std::optional<BlockLocation> EditorDocument::location(BlockID id) const
{
    for (int p = 0; p < m_pages.size(); ++p) {
        const QList<EditorBlock> &blocks = m_pages[p].blocks;
        for (int b = 0; b < blocks.size(); ++b) {
            if (blocks[b].id == id)
                return BlockLocation{p, b};
        }
    }
    return std::nullopt;
}

std::optional<EditorBlock> EditorDocument::block(BlockID id) const
{
    if (auto at = location(id))
        return this->at(*at);
    return std::nullopt;
}

std::optional<EditorBlock> EditorDocument::blockAfter(BlockID id) const
{
    const auto at = location(id);
    if (!at)
        return std::nullopt;
    if (at->block + 1 < m_pages[at->page].blocks.size())
        return m_pages[at->page].blocks[at->block + 1];
    for (int p = at->page + 1; p < m_pages.size(); ++p) {
        if (!m_pages[p].blocks.isEmpty())
            return m_pages[p].blocks.first();
    }
    return std::nullopt;
}

std::optional<EditorBlock> EditorDocument::blockBefore(BlockID id) const
{
    const auto at = location(id);
    if (!at)
        return std::nullopt;
    if (at->block > 0)
        return m_pages[at->page].blocks[at->block - 1];
    for (int p = at->page - 1; p >= 0; --p) {
        if (!m_pages[p].blocks.isEmpty())
            return m_pages[p].blocks.last();
    }
    return std::nullopt;
}

// MARK: Editing

void EditorDocument::setText(const QString &text, BlockID id)
{
    const auto at = location(id);
    if (!at || !this->at(*at).isText())
        return;
    m_pages[at->page].blocks[at->block].textValue = text;
}

void EditorDocument::updateDrawing(BlockID id, const QString &source)
{
    const auto at = location(id);
    if (!at || this->at(*at).kind != BlockKind::drawing)
        return;
    m_pages[at->page].blocks[at->block].drawingValue.source = source;
}

void EditorDocument::updateDeck(BlockID id, CardDeck deck)
{
    const auto at = location(id);
    if (!at || this->at(*at).kind != BlockKind::cards)
        return;
    EditorBlock &block = m_pages[at->page].blocks[at->block];
    deck.id = block.deckValue.id;
    block.deckValue = deck;
}

std::optional<BlockID> EditorDocument::insertDrawing(const QString &source, BlockID splitting, int offset)
{
    const auto target = block(splitting);
    if (!target || !target->isText())
        return std::nullopt;
    return insert(makeDrawingBlock(source), splitting, offset);
}

BlockID EditorDocument::appendDrawing(const QString &source, int toPage)
{
    return append(makeDrawingBlock(source), toPage);
}

std::optional<BlockID> EditorDocument::insertDeck(const CardDeck &deck, BlockID splitting, int offset)
{
    const auto target = block(splitting);
    if (!target || !target->isText())
        return std::nullopt;
    return insert(makeDeckBlock(deck), splitting, offset);
}

BlockID EditorDocument::appendDeck(const CardDeck &deck, int toPage)
{
    return append(makeDeckBlock(deck), toPage);
}

namespace {

QString trimmingNewlines(const QString &text, bool leading)
{
    int start = 0, end = int(text.size());
    if (leading) {
        while (start < end && text[start] == QLatin1Char('\n'))
            ++start;
    } else {
        while (end > start && text[end - 1] == QLatin1Char('\n'))
            --end;
    }
    return text.mid(start, end - start);
}

} // namespace

std::optional<BlockID> EditorDocument::insert(const EditorBlock &block, BlockID splitting, int offset)
{
    const auto at = location(splitting);
    if (!at || !this->at(*at).isText())
        return std::nullopt;
    const QString text = this->at(*at).textValue;
    offset = std::min(std::max(offset, 0), int(text.size()));
    const QString before = trimmingNewlines(text.left(offset), false);
    const QString after = trimmingNewlines(text.mid(offset), true);
    QList<EditorBlock> replacement;
    if (!before.isEmpty())
        replacement.append(EditorBlock::text(splitting, before));
    replacement.append(block);
    replacement.append(EditorBlock::text(before.isEmpty() ? splitting : makeID(), after));
    QList<EditorBlock> &blocks = m_pages[at->page].blocks;
    blocks.removeAt(at->block);
    for (int i = 0; i < replacement.size(); ++i)
        blocks.insert(at->block + i, replacement[i]);
    normalize(at->page);
    return block.id;
}

BlockID EditorDocument::append(const EditorBlock &block, int toPage)
{
    const int page = std::min(std::max(toPage, 0), int(m_pages.size()) - 1);
    int index = int(m_pages[page].blocks.size());
    if (!m_pages[page].blocks.isEmpty() && m_pages[page].blocks.last().isEmptyText())
        --index;
    m_pages[page].blocks.insert(index, block);
    normalize(page);
    return block.id;
}

BlockID EditorDocument::insertPage(int after)
{
    const int index = std::min(std::max(after + 1, 0), int(m_pages.size()));
    const EditorBlock block = EditorBlock::text(makeID(), QString());
    m_pages.insert(index, EditorPage{makePageID(), {block}});
    return block.id;
}

void EditorDocument::removePage(int page)
{
    if (m_pages.size() <= 1 || page < 0 || page >= m_pages.size())
        return;
    const QList<EditorBlock> blocks = m_pages[page].blocks;
    for (const EditorBlock &block : blocks)
        retireID(block);
    m_pages.removeAt(page);
}

void EditorDocument::mergePageWithPrevious(int page)
{
    if (page <= 0 || page >= m_pages.size())
        return;
    const QList<EditorBlock> blocks = m_pages.takeAt(page).blocks;
    m_pages[page - 1].blocks += blocks;
    normalize(page - 1);
}

std::optional<BlockID> EditorDocument::removeBlock(BlockID id)
{
    const auto at = location(id);
    if (!at)
        return std::nullopt;
    retireID(this->at(*at));
    m_pages[at->page].blocks.removeAt(at->block);
    normalize(at->page);
    const QList<EditorBlock> &blocks = m_pages[at->page].blocks;
    return blocks[std::max(0, std::min(at->block - 1, int(blocks.size()) - 1))].id;
}

std::optional<BlockID> EditorDocument::duplicateBlock(BlockID id)
{
    const auto at = location(id);
    if (!at)
        return std::nullopt;
    EditorBlock copy;
    const EditorBlock &source = this->at(*at);
    switch (source.kind) {
    case BlockKind::text: copy = EditorBlock::text(makeID(), source.textValue); break;
    case BlockKind::drawing: copy = makeDrawingBlock(source.drawingValue.source); break;
    case BlockKind::cards: {
        CardDeck deck = source.deckValue;
        deck.id = newDeckID();
        copy = EditorBlock::cards(makeID(), deck);
        break;
    }
    }
    m_pages[at->page].blocks.insert(at->block + 1, copy);
    normalize(at->page);
    return copy.id;
}

bool EditorDocument::moveBlock(BlockID id, int delta)
{
    const auto at = location(id);
    if (!at || (delta != -1 && delta != 1))
        return false;
    QList<EditorBlock> blocks = m_pages[at->page].blocks;
    const int target = at->block + delta;
    // A page's trailing empty text block isn't a real neighbour to swap with.
    const int lastReal = (!blocks.isEmpty() && blocks.last().isEmptyText()) ? int(blocks.size()) - 2 : int(blocks.size()) - 1;
    if (target >= 0 && target <= lastReal) {
        blocks.swapItemsAt(at->block, target);
        m_pages[at->page].blocks = blocks;
        normalize(at->page);
        return true;
    }
    const int page = at->page + delta;
    if (page < 0 || page >= m_pages.size())
        return false;
    const EditorBlock moved = m_pages[at->page].blocks.takeAt(at->block);
    if (delta < 0) {
        const QList<EditorBlock> &destination = m_pages[page].blocks;
        const int end = (!destination.isEmpty() && destination.last().isEmptyText()) ? int(destination.size()) - 1 : int(destination.size());
        m_pages[page].blocks.insert(end, moved);
    } else {
        m_pages[page].blocks.insert(0, moved);
    }
    normalize(at->page);
    normalize(page);
    return true;
}

std::optional<EditorDocument::Merged> EditorDocument::mergeWithPrevious(BlockID id)
{
    const auto at = location(id);
    if (!at || at->block <= 0 || !this->at(*at).isText())
        return std::nullopt;
    const EditorBlock &previousBlock = m_pages[at->page].blocks[at->block - 1];
    if (!previousBlock.isText())
        return std::nullopt;
    const QString text = this->at(*at).textValue;
    const QString previous = previousBlock.textValue;
    const BlockID previousID = previousBlock.id;
    const QString joined = previous.isEmpty() ? text : text.isEmpty() ? previous : previous + QLatin1Char('\n') + text;
    const int offset = previous.isEmpty() ? 0 : int(previous.size()) + (text.isEmpty() ? 0 : 1);
    m_pages[at->page].blocks[at->block - 1].textValue = joined;
    m_pages[at->page].blocks.removeAt(at->block);
    normalize(at->page);
    return Merged{previousID, offset};
}

std::optional<BlockID> EditorDocument::textBlockAfter(BlockID id)
{
    const auto at = location(id);
    if (!at)
        return std::nullopt;
    const int next = at->block + 1;
    if (next < m_pages[at->page].blocks.size() && m_pages[at->page].blocks[next].isText())
        return m_pages[at->page].blocks[next].id;
    const EditorBlock block = EditorBlock::text(makeID(), QString());
    m_pages[at->page].blocks.insert(next, block);
    return block.id;
}

// MARK: External changes

EditorDocument EditorDocument::reconciled(const Note &note) const
{
    EditorDocument result(note, m_lastID);
    QHash<QString, BlockID> blockIDsByNoteID;
    for (const EditorPage &page : m_pages) {
        for (const EditorBlock &block : page.blocks) {
            if (block.kind == BlockKind::drawing)
                blockIDsByNoteID[block.drawingValue.id] = block.id;
            if (block.kind == BlockKind::cards)
                blockIDsByNoteID[block.deckValue.id] = block.id;
        }
    }
    QSet<BlockID> used;
    for (int p = 0; p < result.m_pages.size(); ++p) {
        QList<EditorBlock> oldText;
        if (p < m_pages.size()) {
            for (const EditorBlock &block : m_pages[p].blocks) {
                if (block.isText())
                    oldText.append(block);
            }
            result.m_pages[p].id = m_pages[p].id;
        }
        int textIndex = 0;
        for (int b = 0; b < result.m_pages[p].blocks.size(); ++b) {
            const EditorBlock block = result.m_pages[p].blocks[b];
            std::optional<EditorBlock> match;
            switch (block.kind) {
            case BlockKind::drawing:
                if (auto it = blockIDsByNoteID.constFind(block.drawingValue.id); it != blockIDsByNoteID.constEnd()) {
                    match = block;
                    match->id = it.value();
                }
                break;
            case BlockKind::cards:
                if (auto it = blockIDsByNoteID.constFind(block.deckValue.id); it != blockIDsByNoteID.constEnd()) {
                    match = block;
                    match->id = it.value();
                }
                break;
            case BlockKind::text:
                if (textIndex < oldText.size()) {
                    match = oldText[textIndex];
                    if (canonical(match->textValue) != canonical(block.textValue))
                        match->textValue = block.textValue;
                }
                ++textIndex;
                break;
            }
            if (match && !used.contains(match->id)) {
                used.insert(match->id);
                result.m_pages[p].blocks[b] = *match;
            }
        }
    }
    return result;
}

EditorDocument EditorDocument::restoring(const EditorDocument &snapshot) const
{
    EditorDocument restored = snapshot;
    restored.m_lastID = std::max(m_lastID, snapshot.m_lastID);
    for (const QString &key : {QStringLiteral("last-drawing"), QStringLiteral("last-cards")}) {
        const auto currentValue = m_base.frontMatter.value(key);
        const auto oldValue = snapshot.m_base.frontMatter.value(key);
        const qint64 current = currentValue ? swiftInt(*currentValue).value_or(0) : 0;
        const qint64 old = oldValue ? swiftInt(*oldValue).value_or(0) : 0;
        if (current > old)
            restored.m_base.frontMatter.setValue(key, QString::number(current));
    }
    return restored;
}

// MARK: Helpers

QString EditorDocument::canonical(const QString &text)
{
    if (text.isEmpty())
        return text;
    if (!text.front().isSpace() && !text.back().isSpace())
        return text;
    QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    while (!lines.isEmpty() && isAllWhitespace(lines.first()))
        lines.removeFirst();
    while (!lines.isEmpty() && isAllWhitespace(lines.last()))
        lines.removeLast();
    return lines.join(QLatin1Char('\n'));
}

BlockID EditorDocument::makeID()
{
    ++m_lastID;
    return BlockID{m_lastID};
}

PageID EditorDocument::makePageID()
{
    ++m_lastID;
    return PageID{m_lastID};
}

EditorBlock EditorDocument::makeBlock(const NoteBlock &block)
{
    switch (block.kind) {
    case BlockKind::text: return EditorBlock::text(makeID(), block.text);
    case BlockKind::drawing: return EditorBlock::drawing(makeID(), block.drawing);
    case BlockKind::cards: return EditorBlock::cards(makeID(), block.deck);
    }
    return {};
}

EditorBlock EditorDocument::makeDrawingBlock(const QString &source)
{
    const QString id = newDrawingID();
    return EditorBlock::drawing(makeID(), Drawing(id, source));
}

EditorBlock EditorDocument::makeDeckBlock(CardDeck deck)
{
    deck.id = newDeckID();
    return EditorBlock::cards(makeID(), deck);
}

QString EditorDocument::newDeckID()
{
    Note scratch = note();
    QString id;
    try {
        id = scratch.insertDeck(CardDeck(QList<Flashcard>{}), 1);
    } catch (const NoteEditError &) {
        id = scratch.nextDeckID();
    }
    m_base.frontMatter = scratch.frontMatter;
    return id;
}

QString EditorDocument::newDrawingID()
{
    Note scratch = note();
    QString id;
    try {
        id = scratch.insertDrawing(QString(), 1);
    } catch (const NoteEditError &) {
        id = scratch.nextDrawingID();
    }
    m_base.frontMatter = scratch.frontMatter;
    return id;
}

void EditorDocument::retireID(const EditorBlock &block)
{
    Note scratch = note();
    try {
        switch (block.kind) {
        case BlockKind::drawing: scratch.deleteDrawing(block.drawingValue.id); break;
        case BlockKind::cards: scratch.deleteDeck(block.deckValue.id); break;
        case BlockKind::text: return;
        }
    } catch (const NoteEditError &) {
    }
    m_base.frontMatter = scratch.frontMatter;
}

void EditorDocument::normalize(int p)
{
    QList<EditorBlock> blocks;
    for (const EditorBlock &block : m_pages[p].blocks) {
        if (!blocks.isEmpty() && blocks.last().isEmptyText() && block.isText()) {
            blocks.last() = block;
        } else if (block.isEmptyText() && !blocks.isEmpty() && blocks.last().isText()) {
            continue;
        } else {
            blocks.append(block);
        }
    }
    if (blocks.isEmpty() || !blocks.last().isText())
        blocks.append(EditorBlock::text(makeID(), QString()));
    m_pages[p].blocks = blocks;
}

} // namespace wp
