#pragma once
#include "core/Note.h"

#include <QHash>
#include <QList>
#include <QString>
#include <optional>
#include <utility>

namespace wp {

/// Identity of a block while a note is open in the editor. Views are keyed by
/// it, so a block keeps its view (and its typing undo) across edits.
struct BlockID {
    int rawValue = 0;
    bool operator==(const BlockID &) const = default;
    QString description() const { return QStringLiteral("#%1").arg(rawValue); }
};
inline size_t qHash(const BlockID &id, size_t seed = 0)
{
    return ::qHash(id.rawValue, seed);
}

/// Identity of a page while a note is open in the editor.
struct PageID {
    int rawValue = 0;
    bool operator==(const PageID &) const = default;
};
inline size_t qHash(const PageID &id, size_t seed = 0)
{
    return ::qHash(id.rawValue, seed);
}

struct EditorBlock {
    BlockID id;
    BlockKind kind = BlockKind::text;
    QString textValue;
    Drawing drawingValue;
    CardDeck deckValue;

    static EditorBlock text(BlockID id, QString text)
    {
        EditorBlock b;
        b.id = id;
        b.kind = BlockKind::text;
        b.textValue = std::move(text);
        return b;
    }
    static EditorBlock drawing(BlockID id, Drawing drawing)
    {
        EditorBlock b;
        b.id = id;
        b.kind = BlockKind::drawing;
        b.drawingValue = std::move(drawing);
        return b;
    }
    static EditorBlock cards(BlockID id, CardDeck deck)
    {
        EditorBlock b;
        b.id = id;
        b.kind = BlockKind::cards;
        b.deckValue = std::move(deck);
        return b;
    }

    std::optional<QString> text() const { return kind == BlockKind::text ? std::optional<QString>(textValue) : std::nullopt; }
    std::optional<Drawing> drawing() const { return kind == BlockKind::drawing ? std::optional<Drawing>(drawingValue) : std::nullopt; }
    std::optional<CardDeck> deck() const { return kind == BlockKind::cards ? std::optional<CardDeck>(deckValue) : std::nullopt; }
    bool isText() const { return kind == BlockKind::text; }
    /// A text block with no text.
    bool isEmptyText() const { return kind == BlockKind::text && textValue.isEmpty(); }

    bool operator==(const EditorBlock &) const = default;
};

struct EditorPage {
    PageID id;
    QList<EditorBlock> blocks;
    bool operator==(const EditorPage &) const = default;
};

/// A block's position: 0-based page and block indices.
struct BlockLocation {
    int page = 0;
    int block = 0;
    bool operator==(const BlockLocation &) const = default;
};

/// The editor's model of a note: pages of identified blocks.
///
/// It differs from `Note` in one way: every page ends with a text block, so
/// there's always somewhere to type (an empty page is one empty text block).
/// Empty text blocks are dropped again when converting back to a `Note`.
/// Text offsets are UTF-16, like `QTextDocument` positions.
class EditorDocument
{
public:
    EditorDocument() : EditorDocument(Note()) {}
    explicit EditorDocument(const Note &note);

    const QList<EditorPage> &pages() const { return m_pages; }

    /// The note as it would be saved.
    Note note() const;

    bool operator==(const EditorDocument &other) const
    {
        return m_pages == other.m_pages && m_base == other.m_base && m_lastID == other.m_lastID;
    }

    // MARK: Lookup

    const EditorBlock &at(BlockLocation location) const { return m_pages[location.page].blocks[location.block]; }
    std::optional<BlockLocation> location(BlockID id) const;
    std::optional<EditorBlock> block(BlockID id) const;
    /// The next block in reading order, across pages.
    std::optional<EditorBlock> blockAfter(BlockID id) const;
    /// The previous block in reading order, across pages.
    std::optional<EditorBlock> blockBefore(BlockID id) const;

    // MARK: Editing

    void setText(const QString &text, BlockID id);
    void updateDrawing(BlockID id, const QString &source);
    /// Replaces a deck's title and cards, keeping its deck id.
    void updateDeck(BlockID id, CardDeck deck);

    /// Splits text block `id` at `offset` and puts a new drawing between the
    /// halves. Returns the drawing's block id.
    std::optional<BlockID> insertDrawing(const QString &source, BlockID splitting, int offset);
    /// Adds a drawing at the end of a page, before its trailing empty text block.
    BlockID appendDrawing(const QString &source, int toPage);
    /// Like `insertDrawing`, for a flashcard deck (which gets a fresh deck id).
    std::optional<BlockID> insertDeck(const CardDeck &deck, BlockID splitting, int offset);
    /// Like `appendDrawing`, for a flashcard deck (which gets a fresh deck id).
    BlockID appendDeck(const CardDeck &deck, int toPage);

    /// Inserts an empty page after `page` and returns its text block.
    BlockID insertPage(int after);
    /// Removes a page; the only page can't be removed.
    void removePage(int page);
    /// Joins page `page` onto the end of the page before it (removes the page break).
    void mergePageWithPrevious(int page);

    /// Removes a block. Returns the block that should take focus instead.
    std::optional<BlockID> removeBlock(BlockID id);
    /// Inserts a copy after the block (drawings and decks get a fresh id) and returns it.
    std::optional<BlockID> duplicateBlock(BlockID id);
    /// Moves a block one place up (`-1`) or down (`+1`), crossing into the
    /// neighbouring page at a page edge. Returns false if it can't move.
    bool moveBlock(BlockID id, int delta);

    struct Merged {
        BlockID block;
        int offset = 0;
    };
    /// Joins text block `id` onto the text block before it on the same page.
    /// Returns the merged block and the offset where the joined text starts.
    std::optional<Merged> mergeWithPrevious(BlockID id);

    /// The text block right after drawing `id`, inserting an empty one if the
    /// next block isn't text.
    std::optional<BlockID> textBlockAfter(BlockID id);

    // MARK: External changes

    /// `note` with this document's block and page ids carried over where the
    /// blocks still correspond: drawings by drawing id, text blocks by page
    /// index and position among the page's text blocks, pages by index.
    /// A text block whose content only differs by surrounding blank lines
    /// keeps the editor's text, so the caret doesn't jump.
    EditorDocument reconciled(const Note &note) const;

    /// Takes `snapshot`'s content (for undo) without giving back drawing or
    /// deck ids handed out since, so an id is never reused.
    EditorDocument restoring(const EditorDocument &snapshot) const;

    /// Text as the file format stores it: without leading or trailing blank lines.
    static QString canonical(const QString &text);

private:
    EditorDocument(const Note &note, int lastID);

    BlockID makeID();
    PageID makePageID();
    EditorBlock makeBlock(const NoteBlock &block);
    EditorBlock makeDrawingBlock(const QString &source);
    EditorBlock makeDeckBlock(CardDeck deck);
    QString newDeckID();
    QString newDrawingID();
    void retireID(const EditorBlock &block);
    void normalize(int page);
    std::optional<BlockID> insert(const EditorBlock &block, BlockID splitting, int offset);
    BlockID append(const EditorBlock &block, int toPage);

    QList<EditorPage> m_pages;
    /// Front matter and the drawing and deck id high-water marks; its pages are unused.
    Note m_base;
    int m_lastID = 0;
};

} // namespace wp
