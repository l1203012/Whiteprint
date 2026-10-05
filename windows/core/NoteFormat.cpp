#include "core/Note.h"

#include "core/MarkdownFence.h"
#include "core/TextUtil.h"

#include <QSet>

namespace wp {
namespace {

QString trimTrailing(const QString &line)
{
    qsizetype end = line.size();
    while (end > 0 && (line[end - 1] == QLatin1Char(' ') || line[end - 1] == QLatin1Char('\t')))
        --end;
    return line.left(end);
}

bool isBlank(const QString &line)
{
    for (QChar c : line) {
        if (!c.isSpace())
            return false;
    }
    return true;
}

bool isBlockFenceInfo(const QString &info)
{
    return info == Note::drawingFenceInfo || info == Note::cardsFenceInfo;
}

QStringList splitNormalizedLines(const QString &text)
{
    QString normalized = text;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return normalized.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
}

/// Splits the body (everything after the front matter) into pages and blocks.
class BodyParser
{
public:
    void consume(const QString &line)
    {
        if (fence) {
            if (fence->isClosed(line)) {
                fence.reset();
                if (special) {
                    finishSpecialBlock();
                    return;
                }
            }
            if (special)
                special->lines.append(line);
            else
                text.append(line);
            return;
        }

        if (trimTrailing(line) == Note::pageSeparator) {
            finishPage();
            return;
        }

        if (auto open = MarkdownFence::opening(line)) {
            fence = open;
            const QStringList words = open->infoWords();
            if (!words.isEmpty() && isBlockFenceInfo(words.first())) {
                flushText();
                // A missing id is filled in by `normalizeBlockIDs`.
                special = Special{words.first(), open->attribute(QStringLiteral("id")).value_or(QString()), {}};
                return;
            }
        }
        text.append(line);
    }

    QList<NotePage> finish()
    {
        if (special)
            finishSpecialBlock();
        finishPage();
        return pages;
    }

private:
    struct Special {
        QString info;
        QString id;
        QStringList lines;
    };

    void finishSpecialBlock()
    {
        if (!special)
            return;
        const QString source = special->lines.join(QLatin1Char('\n'));
        if (special->info == Note::cardsFenceInfo)
            blocks.append(NoteBlock::cardsBlock(CardDeck::parsing(special->id, source)));
        else
            blocks.append(NoteBlock::drawingBlock(Drawing(special->id, source)));
        special.reset();
    }

    void finishPage()
    {
        flushText();
        pages.append(NotePage(blocks));
        blocks.clear();
    }

    /// Adds the buffered lines as a text block, without surrounding blank lines.
    void flushText()
    {
        qsizetype first = -1, last = -1;
        for (qsizetype i = 0; i < text.size(); ++i) {
            if (!isBlank(text[i])) {
                if (first < 0)
                    first = i;
                last = i;
            }
        }
        if (first >= 0)
            blocks.append(NoteBlock::textBlock(text.mid(first, last - first + 1).join(QLatin1Char('\n'))));
        text.clear();
    }

    QList<NotePage> pages;
    QList<NoteBlock> blocks;
    QStringList text;
    std::optional<MarkdownFence> fence;
    /// An open ```` ```wp ```` or ```` ```cards ```` block.
    std::optional<Special> special;
};

QList<NotePage> parsePages(const QStringList &lines, qsizetype from)
{
    BodyParser body;
    for (qsizetype i = from; i < lines.size(); ++i)
        body.consume(lines[i]);
    return body.finish();
}

/// One backtick longer than any backtick run that starts a line of the source.
int fenceLength(const QString &source)
{
    int longest = 0;
    for (const QStringView &line : QStringView(source).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        qsizetype i = 0;
        while (i < line.size() && line[i] == QLatin1Char(' '))
            ++i;
        qsizetype run = 0;
        while (i + run < line.size() && line[i + run] == QLatin1Char('`'))
            ++run;
        longest = std::max(longest, int(run));
    }
    return std::max(3, longest + 1);
}

QString fenced(const QString &source, const QString &info, const QString &id)
{
    const QString fence = QString(fenceLength(source), QLatin1Char('`'));
    const QString content = source.isEmpty() ? QString() : source + QLatin1Char('\n');
    return QStringLiteral("%1%2 id=%3\n%4%1").arg(fence, info, id, content);
}

/// Text typed by someone can contain a line that would read back as
/// structure: a `+++page` separator or a ```` ```wp ```` / ```` ```cards ````
/// opener outside a code block. Such lines get a leading `\`, which keeps
/// them literal in Markdown too.
QString escapingStructure(const QString &text)
{
    std::optional<MarkdownFence> open;
    bool changed = false;
    QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (QString &line : lines) {
        if (open) {
            if (open->isClosed(line))
                open.reset();
            continue;
        }
        if (trimTrailing(line) == Note::pageSeparator) {
            changed = true;
            line = QLatin1Char('\\') + line;
            continue;
        }
        if (auto fence = MarkdownFence::opening(line)) {
            const QStringList words = fence->infoWords();
            if (!words.isEmpty() && isBlockFenceInfo(words.first())) {
                changed = true;
                line = QLatin1Char('\\') + line;
                continue;
            }
            open = fence;
        }
    }
    return changed ? lines.join(QLatin1Char('\n')) : text;
}

QString serializeBlock(const NoteBlock &block)
{
    switch (block.kind) {
    case BlockKind::text: {
        const QString text = escapingStructure(block.text);
        // Close a fence the text left open, so it can't swallow the blocks after it.
        const auto open = MarkdownFence::unclosed(text);
        if (!open)
            return text;
        return text + QLatin1Char('\n') + QString(open->length, open->marker);
    }
    case BlockKind::drawing:
        return fenced(block.drawing.source, Note::drawingFenceInfo, block.drawing.id);
    case BlockKind::cards:
        return fenced(block.deck.source(), Note::cardsFenceInfo, block.deck.id);
    }
    return {};
}

} // namespace

// MARK: - Parsing

Note Note::parsing(const QString &text)
{
    const QStringList lines = splitNormalizedLines(text);
    qsizetype start = 0;

    FrontMatter frontMatter;
    if (!lines.isEmpty() && trimTrailing(lines.first()) == QLatin1String("---")) {
        qsizetype close = -1;
        for (qsizetype i = 1; i < lines.size(); ++i) {
            if (trimTrailing(lines[i]) == QLatin1String("---")) {
                close = i;
                break;
            }
        }
        if (close >= 0) {
            QList<FrontMatter::Field> fields;
            for (qsizetype i = 1; i < close; ++i) {
                const qsizetype colon = lines[i].indexOf(QLatin1Char(':'));
                if (colon < 0)
                    continue;
                const QString key = trimmedWhitespace(QStringView(lines[i]).left(colon));
                const QString value = trimmedWhitespace(QStringView(lines[i]).mid(colon + 1));
                if (!key.isEmpty())
                    fields.append({key, value});
            }
            frontMatter = FrontMatter(fields);
            start = close + 1;
        }
    }

    const QString rawVersion = frontMatter.value(FrontMatter::versionKey).value_or(QString());
    const auto version = swiftInt(trimmedWhitespace(rawVersion));
    if (!version)
        throw NoteFormatError::invalidVersion(rawVersion);
    if (*version > Note::formatVersion)
        throw NoteFormatError::unsupportedVersion(int(*version));

    return Note(frontMatter, parsePages(lines, start));
}

QList<NotePage> Note::parseBody(const QString &text)
{
    return parsePages(splitNormalizedLines(text), 0);
}

// MARK: - Serializing

QString Note::serialized() const
{
    QString out = QStringLiteral("---\n");
    for (const FrontMatter::Field &field : frontMatter.fields())
        out += field.key + QStringLiteral(": ") + field.value + QLatin1Char('\n');
    out += QStringLiteral("---\n");
    QStringList bodies;
    for (const NotePage &page : m_pages)
        bodies.append(serialize(page));
    const QString body = bodies.join(QStringLiteral("\n\n") + pageSeparator + QStringLiteral("\n\n"));
    if (!body.isEmpty())
        out += QLatin1Char('\n') + body + QLatin1Char('\n');
    return out;
}

QString Note::serialize(const NotePage &page)
{
    QStringList parts;
    for (const NoteBlock &block : page.blocks)
        parts.append(serializeBlock(block));
    return parts.join(QLatin1String("\n\n"));
}

} // namespace wp
