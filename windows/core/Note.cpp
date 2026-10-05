#include "core/Note.h"

#include "core/TextUtil.h"

#include <QSet>
#include <algorithm>

namespace wp {

// MARK: Block ids

QString blockIDPrefix(BlockIDKind kind)
{
    return kind == BlockIDKind::drawing ? QStringLiteral("d") : QStringLiteral("c");
}

std::optional<BlockIDKind> blockIDKindOf(const QString &id)
{
    if (id.startsWith(QLatin1Char('d')))
        return BlockIDKind::drawing;
    if (id.startsWith(QLatin1Char('c')))
        return BlockIDKind::cards;
    return std::nullopt;
}

qint64 highestBlockIDNumber(BlockIDKind kind, const QStringList &ids)
{
    const QString prefix = blockIDPrefix(kind);
    qint64 highest = 0;
    for (const QString &id : ids) {
        if (!id.startsWith(prefix))
            continue;
        if (auto n = swiftInt(QStringView(id).mid(1)))
            highest = std::max(highest, *n);
    }
    return highest;
}

bool Drawing::isValidID(const QString &id)
{
    if (id.isEmpty())
        return false;
    for (QChar c : id) {
        const char16_t u = c.unicode();
        const bool ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '_'
            || u == '-';
        if (!ok)
            return false;
    }
    return true;
}

std::optional<QString> NoteBlock::id() const
{
    switch (kind) {
    case BlockKind::text: return std::nullopt;
    case BlockKind::drawing: return drawing.id;
    case BlockKind::cards: return deck.id;
    }
    return std::nullopt;
}

std::optional<BlockIDKind> NoteBlock::idKind() const
{
    switch (kind) {
    case BlockKind::text: return std::nullopt;
    case BlockKind::drawing: return BlockIDKind::drawing;
    case BlockKind::cards: return BlockIDKind::cards;
    }
    return std::nullopt;
}

NoteBlock NoteBlock::withID(const QString &id) const
{
    NoteBlock copy = *this;
    if (kind == BlockKind::drawing)
        copy.drawing.id = id;
    else if (kind == BlockKind::cards)
        copy.deck.id = id;
    return copy;
}

// MARK: FrontMatter

FrontMatter::FrontMatter() : FrontMatter(QList<Field>()) {}

FrontMatter::FrontMatter(QList<Field> fields)
{
    qsizetype versionIndex = -1;
    for (qsizetype i = 0; i < fields.size(); ++i) {
        if (fields[i].key == versionKey) {
            versionIndex = i;
            break;
        }
    }
    if (versionIndex >= 0)
        fields.move(versionIndex, 0);
    else
        fields.prepend(Field{versionKey, QString::number(Note::formatVersion)});
    m_fields = std::move(fields);
}

std::optional<QString> FrontMatter::value(const QString &key) const
{
    for (const Field &f : m_fields) {
        if (f.key == key)
            return f.value;
    }
    return std::nullopt;
}

void FrontMatter::setValue(const QString &key, const std::optional<QString> &newValue)
{
    if (key == versionKey)
        return;
    if (newValue) {
        // Values are single-line by definition.
        QString v = *newValue;
        v.replace(QLatin1Char('\n'), QLatin1Char(' '));
        for (Field &f : m_fields) {
            if (f.key == key) {
                f.value = v;
                return;
            }
        }
        m_fields.append(Field{key, v});
    } else {
        m_fields.removeIf([&](const Field &f) { return f.key == key; });
    }
}

std::optional<int> FrontMatter::formatVersion() const
{
    if (auto v = value(versionKey)) {
        if (auto n = swiftInt(*v))
            return int(*n);
    }
    return std::nullopt;
}

qint64 FrontMatter::lastNumber(BlockIDKind kind) const
{
    if (auto v = value(lastNumberKey(kind))) {
        if (auto n = swiftInt(*v))
            return *n;
    }
    return 0;
}

void FrontMatter::setLastNumber(qint64 number, BlockIDKind kind)
{
    setValue(lastNumberKey(kind), number > 0 ? std::optional<QString>(QString::number(number)) : std::nullopt);
}

QString FrontMatter::lastNumberKey(BlockIDKind kind)
{
    return kind == BlockIDKind::drawing ? QStringLiteral("last-drawing") : QStringLiteral("last-cards");
}

// MARK: Note

Note::Note() : Note(FrontMatter(), {NotePage()}) {}

Note::Note(QList<NotePage> pages) : Note(FrontMatter(), std::move(pages)) {}

Note::Note(const QString &title, QList<NotePage> pages)
{
    frontMatter.setTitle(title);
    m_pages = pages.isEmpty() ? QList<NotePage>{NotePage()} : std::move(pages);
    normalizeBlockIDs();
}

Note::Note(FrontMatter fm, QList<NotePage> pages) : frontMatter(std::move(fm))
{
    m_pages = pages.isEmpty() ? QList<NotePage>{NotePage()} : std::move(pages);
    normalizeBlockIDs();
}

void Note::setPages(QList<NotePage> pages)
{
    m_pages = pages.isEmpty() ? QList<NotePage>{NotePage()} : std::move(pages);
}

QList<Drawing> Note::drawings() const
{
    QList<Drawing> result;
    for (const NotePage &page : m_pages) {
        for (const NoteBlock &block : page.blocks) {
            if (block.kind == BlockKind::drawing)
                result.append(block.drawing);
        }
    }
    return result;
}

QList<CardDeck> Note::decks() const
{
    QList<CardDeck> result;
    for (const NotePage &page : m_pages) {
        for (const NoteBlock &block : page.blocks) {
            if (block.kind == BlockKind::cards)
                result.append(block.deck);
        }
    }
    return result;
}

QString Note::nextDrawingID() const
{
    return nextID(BlockIDKind::drawing);
}

QString Note::nextDeckID() const
{
    return nextID(BlockIDKind::cards);
}

QString Note::nextID(BlockIDKind kind) const
{
    return blockIDPrefix(kind) + QString::number(highestNumber(kind) + 1);
}

qint64 Note::highestNumber(BlockIDKind kind) const
{
    return std::max(highestBlockIDNumber(kind, idsOf(kind)), frontMatter.lastNumber(kind));
}

QStringList Note::idsOf(BlockIDKind kind) const
{
    QStringList ids;
    for (const NotePage &page : m_pages) {
        for (const NoteBlock &block : page.blocks) {
            if (block.idKind() == kind)
                ids.append(*block.id());
        }
    }
    return ids;
}

void Note::reserveID(const QString &id)
{
    const auto kind = blockIDKindOf(id);
    if (!kind)
        return;
    const qint64 number = highestBlockIDNumber(*kind, {id});
    if (number > frontMatter.lastNumber(*kind))
        frontMatter.setLastNumber(number, *kind);
}

void Note::normalizeBlockIDs()
{
    for (BlockIDKind kind : {BlockIDKind::drawing, BlockIDKind::cards}) {
        QSet<QString> used;
        QList<std::pair<int, int>> missing;
        for (int p = 0; p < m_pages.size(); ++p) {
            for (int b = 0; b < m_pages[p].blocks.size(); ++b) {
                const NoteBlock &block = m_pages[p].blocks[b];
                if (block.idKind() != kind)
                    continue;
                const QString id = block.id().value_or(QString());
                if (Drawing::isValidID(id) && !used.contains(id))
                    used.insert(id);
                else
                    missing.append({p, b});
            }
        }
        qint64 next = std::max(highestBlockIDNumber(kind, QStringList(used.begin(), used.end())),
                               frontMatter.lastNumber(kind)) + 1;
        for (const auto &[p, b] : missing) {
            m_pages[p].blocks[b] = m_pages[p].blocks[b].withID(blockIDPrefix(kind) + QString::number(next));
            ++next;
        }
        for (const QString &id : idsOf(kind))
            reserveID(id);
    }
}

} // namespace wp
