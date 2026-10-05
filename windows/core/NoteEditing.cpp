#include "core/Note.h"

#include <QSet>

namespace wp {

QStringList Note::blockIDs(const QList<NotePage> &pages)
{
    QStringList ids;
    for (const NotePage &page : pages) {
        for (const NoteBlock &block : page.blocks) {
            if (auto id = block.id())
                ids.append(*id);
        }
    }
    return ids;
}

int Note::index(int page) const
{
    if (page < 1 || page > m_pages.size())
        throw NoteEditError::pageOutOfRange(page, int(m_pages.size()));
    return page - 1;
}

std::optional<Note::BlockPos> Note::findLocation(const QString &id, BlockIDKind kind) const
{
    for (int p = 0; p < m_pages.size(); ++p) {
        const QList<NoteBlock> &blocks = m_pages[p].blocks;
        for (int b = 0; b < blocks.size(); ++b) {
            if (blocks[b].idKind() == kind && blocks[b].id() == id)
                return BlockPos{p, b};
        }
    }
    return std::nullopt;
}

Note::BlockPos Note::location(const QString &id, BlockIDKind kind) const
{
    if (auto pos = findLocation(id, kind))
        return *pos;
    throw kind == BlockIDKind::cards ? NoteEditError::unknownDeck(id) : NoteEditError::unknownDrawing(id);
}

QString Note::pageSource(int page) const
{
    return serialize(m_pages[index(page)]);
}

void Note::write(const QString &text, int page, WriteMode mode)
{
    const int i = index(page);
    QList<NotePage> written = parseBody(text);
    QSet<QString> replacedIDs;
    if (mode == WriteMode::replace) {
        for (const QString &id : blockIDs({m_pages[i]}))
            replacedIDs.insert(id);
    }
    QList<NotePage> others;
    for (int p = 0; p < m_pages.size(); ++p) {
        if (p != i)
            others.append(m_pages[p]);
    }
    QSet<QString> keptElsewhere;
    for (const QString &id : blockIDs(others))
        keptElsewhere.insert(id);
    QSet<QString> claimed;
    for (NotePage &writtenPage : written) {
        for (NoteBlock &block : writtenPage.blocks) {
            const auto id = block.id();
            if (!id)
                continue;
            if (replacedIDs.contains(*id) && !keptElsewhere.contains(*id) && !claimed.contains(*id))
                claimed.insert(*id);
            else
                block = block.withID(QString());
        }
    }

    switch (mode) {
    case WriteMode::append: m_pages[i].blocks += written[0].blocks; break;
    case WriteMode::replace: m_pages[i].blocks = written[0].blocks; break;
    }
    for (int k = 1; k < written.size(); ++k)
        m_pages.insert(i + k, written[k]);
    normalizeBlockIDs();
}

QString Note::insertDrawing(const QString &source, int page, const std::optional<QString> &after)
{
    return insertBlock(NoteBlock::drawingBlock(Drawing(nextDrawingID(), source)), page, after);
}

void Note::updateDrawing(const QString &id, const QString &source)
{
    const BlockPos pos = location(id, BlockIDKind::drawing);
    m_pages[pos.page].blocks[pos.block] = NoteBlock::drawingBlock(Drawing(id, source));
}

void Note::deleteDrawing(const QString &id)
{
    const BlockPos pos = location(id, BlockIDKind::drawing);
    reserveID(id);
    m_pages[pos.page].blocks.removeAt(pos.block);
}

std::optional<Note::DrawingLocation> Note::drawing(const QString &id) const
{
    const auto pos = findLocation(id, BlockIDKind::drawing);
    if (!pos)
        return std::nullopt;
    const NoteBlock &block = m_pages[pos->page].blocks[pos->block];
    if (block.kind != BlockKind::drawing)
        return std::nullopt;
    return DrawingLocation{block.drawing, pos->page + 1};
}

QString Note::insertDeck(CardDeck deck, int page, const std::optional<QString> &after)
{
    deck.id = nextDeckID();
    return insertBlock(NoteBlock::cardsBlock(std::move(deck)), page, after);
}

void Note::updateDeck(const QString &id, CardDeck deck)
{
    const BlockPos pos = location(id, BlockIDKind::cards);
    deck.id = id;
    m_pages[pos.page].blocks[pos.block] = NoteBlock::cardsBlock(std::move(deck));
}

void Note::deleteDeck(const QString &id)
{
    const BlockPos pos = location(id, BlockIDKind::cards);
    reserveID(id);
    m_pages[pos.page].blocks.removeAt(pos.block);
}

std::optional<Note::DeckLocation> Note::deck(const QString &id) const
{
    const auto pos = findLocation(id, BlockIDKind::cards);
    if (!pos)
        return std::nullopt;
    const NoteBlock &block = m_pages[pos->page].blocks[pos->block];
    if (block.kind != BlockKind::cards)
        return std::nullopt;
    return DeckLocation{block.deck, pos->page + 1};
}

QString Note::insertBlock(const NoteBlock &block, int page, const std::optional<QString> &after)
{
    const int i = index(page);
    if (after) {
        QList<NoteBlock> &blocks = m_pages[i].blocks;
        int b = -1;
        for (int k = 0; k < blocks.size(); ++k) {
            if (blocks[k].id() == after) {
                b = k;
                break;
            }
        }
        if (b < 0) {
            throw after->startsWith(QLatin1Char('c')) ? NoteEditError::unknownDeck(*after)
                                                      : NoteEditError::unknownDrawing(*after);
        }
        blocks.insert(b + 1, block);
    } else {
        m_pages[i].blocks.append(block);
    }
    const QString id = block.id().value_or(QString());
    reserveID(id);
    return id;
}

int Note::addPage(std::optional<int> after)
{
    const int i = after ? index(*after) + 1 : int(m_pages.size());
    m_pages.insert(i, NotePage());
    return i + 1;
}

void Note::removePage(int page)
{
    const int i = index(page);
    if (m_pages.size() <= 1)
        throw NoteEditError::lastPage();
    for (const QString &id : blockIDs({m_pages[i]}))
        reserveID(id);
    m_pages.removeAt(i);
}

} // namespace wp
