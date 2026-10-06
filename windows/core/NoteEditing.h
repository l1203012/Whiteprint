#pragma once
#include <QString>
#include <optional>

namespace wp {

/// Edits used by the MCP tools and the editor (the `Note` methods are declared in core/Note.h).
/// Page numbers are 1-based, matching what Claude sees in `read_note`.
struct NoteEditError {
    enum class Kind { pageOutOfRange, unknownDrawing, unknownDeck, lastPage };

    Kind kind = Kind::lastPage;
    int page = 0;      // pageOutOfRange
    int pageCount = 0; // pageOutOfRange
    QString id;        // unknownDrawing, unknownDeck

    static NoteEditError pageOutOfRange(int page, int pageCount) { return {Kind::pageOutOfRange, page, pageCount, {}}; }
    static NoteEditError unknownDrawing(QString id) { return {Kind::unknownDrawing, 0, 0, std::move(id)}; }
    static NoteEditError unknownDeck(QString id) { return {Kind::unknownDeck, 0, 0, std::move(id)}; }
    static NoteEditError lastPage() { return {Kind::lastPage, 0, 0, {}}; }
    bool operator==(const NoteEditError &) const = default;

    QString description() const
    {
        switch (kind) {
        case Kind::pageOutOfRange:
            return QStringLiteral("page %1 doesn't exist (note has %2 page%3)")
                .arg(page).arg(pageCount).arg(pageCount == 1 ? QString() : QStringLiteral("s"));
        case Kind::unknownDrawing: return QStringLiteral("no drawing '%1' in this note").arg(id);
        case Kind::unknownDeck: return QStringLiteral("no flashcard deck '%1' in this note").arg(id);
        case Kind::lastPage: return QStringLiteral("can't remove the only page");
        }
        return {};
    }
};

enum class WriteMode { append, replace };

inline QString writeModeName(WriteMode mode)
{
    return mode == WriteMode::append ? QStringLiteral("append") : QStringLiteral("replace");
}

inline std::optional<WriteMode> writeModeFromName(const QString &name)
{
    if (name == QLatin1String("append"))
        return WriteMode::append;
    if (name == QLatin1String("replace"))
        return WriteMode::replace;
    return std::nullopt;
}

} // namespace wp
