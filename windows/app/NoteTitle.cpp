#include "app/NoteTitle.h"

#include "core/TextUtil.h"

#include <QFileInfo>

namespace wp::NoteTitle {

QString display(const Note &note, const QString &filePath)
{
    if (const auto title = note.frontMatter.title()) {
        const QString trimmed = trimmedWhitespace(*title);
        if (!trimmed.isEmpty())
            return trimmed;
    }
    return filePath.isEmpty() ? untitled : baseName(filePath);
}

std::optional<QString> titleAfterRename(const QString &oldPath, const QString &newPath)
{
    if (oldPath.isEmpty() || newPath.isEmpty())
        return std::nullopt;
    const QString newName = baseName(newPath);
    if (baseName(oldPath) == newName)
        return std::nullopt;
    return newName;
}

QString baseName(const QString &path)
{
    // Swift's deletingPathExtension drops only the last extension; QFileInfo::completeBaseName agrees.
    return QFileInfo(path).completeBaseName();
}

} // namespace wp::NoteTitle
