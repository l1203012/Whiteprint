#pragma once
#include "core/CardDeck.h"
#include "core/NoteEditing.h"
#include "core/NoteFormat.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace wp {

/// Blocks that carry a stable id: drawings (`d1`) and flashcard decks (`c1`).
enum class BlockIDKind { drawing, cards };

/// `d` / `c`.
QString blockIDPrefix(BlockIDKind kind);
/// The kind an id belongs to, from its first letter.
std::optional<BlockIDKind> blockIDKindOf(const QString &id);
/// Highest number among the ids with this kind's prefix, or 0.
qint64 highestBlockIDNumber(BlockIDKind kind, const QStringList &ids);

struct Drawing {
    /// Stable id, unique within the note (e.g. `d3`). Used by the MCP tools.
    QString id;
    /// Drawing language source, stored verbatim.
    QString source;

    Drawing() = default;
    Drawing(QString id, QString source) : id(std::move(id)), source(std::move(source)) {}
    bool operator==(const Drawing &) const = default;

    static bool isValidID(const QString &id);
};

enum class BlockKind { text, drawing, cards };

struct NoteBlock {
    BlockKind kind = BlockKind::text;
    /// Markdown text (kind == text). Never contains a top-level ```` ```wp ```` or
    /// ```` ```cards ```` fence or a `+++page` line.
    QString text;
    Drawing drawing; // kind == drawing
    CardDeck deck;   // kind == cards

    static NoteBlock textBlock(QString text)
    {
        NoteBlock b;
        b.kind = BlockKind::text;
        b.text = std::move(text);
        return b;
    }
    static NoteBlock drawingBlock(Drawing drawing)
    {
        NoteBlock b;
        b.kind = BlockKind::drawing;
        b.drawing = std::move(drawing);
        return b;
    }
    static NoteBlock cardsBlock(CardDeck deck)
    {
        NoteBlock b;
        b.kind = BlockKind::cards;
        b.deck = std::move(deck);
        return b;
    }
    bool operator==(const NoteBlock &) const = default;

    /// The block's stable id; nullopt for text.
    std::optional<QString> id() const;
    std::optional<BlockIDKind> idKind() const;
    NoteBlock withID(const QString &id) const;
};

struct NotePage {
    QList<NoteBlock> blocks;

    NotePage() = default;
    explicit NotePage(QList<NoteBlock> blocks) : blocks(std::move(blocks)) {}
    bool operator==(const NotePage &) const = default;
};

/// The `key: value` header at the top of a `.wprint` file. Field order is preserved.
class FrontMatter
{
public:
    struct Field {
        QString key;
        QString value;
        bool operator==(const Field &) const = default;
    };

    static inline const QString versionKey = QStringLiteral("whiteprint");

    FrontMatter();
    /// The format version field is always present and always first.
    explicit FrontMatter(QList<Field> fields);

    const QList<Field> &fields() const { return m_fields; }

    std::optional<QString> value(const QString &key) const;
    /// nullopt removes the field. The version field can't be changed or removed.
    /// Values are single-line: newlines become spaces.
    void setValue(const QString &key, const std::optional<QString> &value);

    std::optional<QString> title() const { return value(QStringLiteral("title")); }
    void setTitle(const std::optional<QString> &title) { setValue(QStringLiteral("title"), title); }

    std::optional<int> formatVersion() const;

    /// Highest id number ever handed out in this note for that kind of block,
    /// stored as `last-drawing` / `last-cards`.
    qint64 lastNumber(BlockIDKind kind) const;
    void setLastNumber(qint64 number, BlockIDKind kind);

    bool operator==(const FrontMatter &) const = default;

private:
    static QString lastNumberKey(BlockIDKind kind);
    QList<Field> m_fields;
};

/// A Whiteprint note: the in-memory form of a `.wprint` file.
///
/// A note is a list of pages. Each page is a list of blocks: Markdown text,
/// drawings written in the Whiteprint drawing language, or flashcard decks.
class Note
{
public:
    /// The newest `.wprint` format version this build can read and write.
    static constexpr int formatVersion = 1;

    FrontMatter frontMatter;

    Note();
    explicit Note(QList<NotePage> pages);
    Note(const QString &title, QList<NotePage> pages = {NotePage()});
    /// Drawings and decks with a missing, invalid or duplicate id get a fresh one.
    Note(FrontMatter frontMatter, QList<NotePage> pages);

    /// Always contains at least one page.
    const QList<NotePage> &pages() const { return m_pages; }
    /// Replaces the pages; an empty list becomes one empty page.
    void setPages(QList<NotePage> pages);
    /// Mutable access to one page (0-based). Pages are added and removed with addPage/removePage.
    NotePage &page(int index) { return m_pages[index]; }

    bool operator==(const Note &) const = default;

    // MARK: Format (.wprint)

    /// Parses `.wprint` text. Parsing is lenient: anything that isn't front matter,
    /// a page separator or a drawing is kept as Markdown, and an unterminated
    /// drawing fence runs to the end of the file. Throws NoteFormatError for an unreadable version.
    static Note parsing(const QString &text);

    /// Parses `.wprint` body text (no front matter) into pages. Drawing ids
    /// are kept as written, or empty when missing.
    static QList<NotePage> parseBody(const QString &text);

    /// Writes canonical `.wprint` text: front matter, then pages separated by
    /// `+++page`, with blocks separated by one blank line.
    QString serialized() const;

    /// One page's blocks in `.wprint` syntax, separated by blank lines.
    static QString serialize(const NotePage &page);

    static inline const QString pageSeparator = QStringLiteral("+++page");
    static inline const QString drawingFenceInfo = QStringLiteral("wp");
    static inline const QString cardsFenceInfo = QStringLiteral("cards");

    // MARK: Content

    /// Every drawing in the note, in reading order.
    QList<Drawing> drawings() const;
    /// Every flashcard deck in the note, in reading order.
    QList<CardDeck> decks() const;

    /// An unused drawing id (`d1`, `d2`, ...). Ids are never reused within a note,
    /// even after a drawing is deleted, so a stale id held by Claude can't point
    /// at a different drawing. The high-water mark is kept in the front matter.
    QString nextDrawingID() const;
    /// An unused deck id (`c1`, `c2`, ...), never reused like drawing ids.
    QString nextDeckID() const;
    QString nextID(BlockIDKind kind) const;

    /// Records `id` as used so it's never handed out again.
    void reserveID(const QString &id);
    /// Gives every drawing and deck a valid, unique id, keeping existing ones where possible.
    void normalizeBlockIDs();

    // MARK: Editing (page numbers are 1-based; all throw NoteEditError)

    struct DrawingLocation {
        Drawing drawing;
        int page = 0;
    };
    struct DeckLocation {
        CardDeck deck;
        int page = 0;
    };

    /// The page's content in `.wprint` syntax, drawings included with their ids.
    QString pageSource(int page) const;

    /// Writes `.wprint` body text to a page. The text may contain ```` ```wp ````
    /// drawings and `+++page` separators; extra pages are inserted after this one.
    ///
    /// New drawings get fresh ids. With `replace`, a drawing written back with
    /// an id it had on this page keeps that id, so read -> edit -> write is stable.
    void write(const QString &text, int page, WriteMode mode);

    /// Inserts a drawing at the end of the page, or right after the drawing or
    /// deck `after`. Returns the new drawing's id.
    QString insertDrawing(const QString &source, int page, const std::optional<QString> &after = std::nullopt);
    void updateDrawing(const QString &id, const QString &source);
    void deleteDrawing(const QString &id);
    /// The drawing with `id`, and the 1-based page it's on.
    std::optional<DrawingLocation> drawing(const QString &id) const;

    /// Inserts a flashcard deck at the end of the page, or right after the
    /// drawing or deck `after`. The deck's own id is ignored; returns the new one.
    QString insertDeck(CardDeck deck, int page, const std::optional<QString> &after = std::nullopt);
    /// Replaces a deck's title and cards, keeping its id.
    void updateDeck(const QString &id, CardDeck deck);
    void deleteDeck(const QString &id);
    /// The deck with `id`, and the 1-based page it's on.
    std::optional<DeckLocation> deck(const QString &id) const;

    /// Adds an empty page after `after` (default: at the end). Returns its number.
    int addPage(std::optional<int> after = std::nullopt);
    void removePage(int page);

    // MARK: Markdown export

    /// Plain Markdown for export. Drawings are left out (or replaced by
    /// `drawingPlaceholder`), front matter is dropped, and pages are separated
    /// by a horizontal rule. The title becomes an H1 unless the note already
    /// starts with that heading.
    QString markdown(const std::optional<QString> &drawingPlaceholder = QStringLiteral("*[drawing omitted]*")) const;

private:
    struct BlockPos {
        int page;
        int block;
    };

    int index(int page) const;
    BlockPos location(const QString &id, BlockIDKind kind) const;
    std::optional<BlockPos> findLocation(const QString &id, BlockIDKind kind) const;
    QString insertBlock(const NoteBlock &block, int page, const std::optional<QString> &after);
    QStringList idsOf(BlockIDKind kind) const;
    qint64 highestNumber(BlockIDKind kind) const;
    static QStringList blockIDs(const QList<NotePage> &pages);
    static QString markdown(const CardDeck &deck);

    QList<NotePage> m_pages;
};

} // namespace wp
