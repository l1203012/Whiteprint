#include "app/NoteFiles.h"

#include "app/AppUtil.h"
#include "app/NotesFolder.h"
#include "core/TextUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <filesystem>
#include <system_error>

namespace wp::NoteFiles {

QString FileError::description() const
{
    switch (kind) {
    case Kind::intoItself: return QStringLiteral("“%1” can’t be moved into itself.").arg(name);
    case Kind::nameTaken: return QStringLiteral("An item named “%1” already exists here.").arg(name);
    case Kind::badName: return QStringLiteral("“%1” can’t be used as a name.").arg(name);
    case Kind::failed: return name;
    }
    return {};
}

namespace {

std::filesystem::path fsPath(const QString &path)
{
    return std::filesystem::path(QDir::toNativeSeparators(path).toStdWString());
}

/// Renames on the same volume, overwriting nothing (callers check the destination first).
void movePath(const QString &from, const QString &to)
{
    std::error_code error;
    std::filesystem::rename(fsPath(from), fsPath(to), error);
    if (error)
        throw FileError::failed(QStringLiteral("“%1” can’t be moved: %2")
                                    .arg(QFileInfo(from).fileName(), QString::fromStdString(error.message())));
}

} // namespace

QString move(const QString &itemPath, const QString &folderPath)
{
    const QString item = canonicalFile(itemPath);
    const QString folder = canonicalFile(folderPath);
    if (pathKey(deletingLastPathComponent(item)) == pathKey(folder))
        return item;
    if (isInside(folder, item))
        throw FileError::intoItself(QFileInfo(item).fileName());
    const QFileInfo info(item);
    const bool isFolder = info.isDir();
    const QString name = info.fileName();
    const QString destination = isFolder || info.suffix().isEmpty()
        ? NotesFolder::unusedURL(name, std::nullopt, folder)
        : NotesFolder::unusedURL(info.completeBaseName(), info.suffix(), folder);
    movePath(item, destination);
    return canonicalFile(destination);
}

QString rename(const QString &itemPath, const QString &name)
{
    const QString item = canonicalFile(itemPath);
    if (trimmedWhitespace(name).isEmpty())
        throw FileError::badName(name);
    const QString clean = NotesFolder::fileName(name);
    const QFileInfo info(item);
    const bool isFolder = info.isDir();
    QString destinationName = clean;
    if (!isFolder && !info.suffix().isEmpty())
        destinationName += QLatin1Char('.') + info.suffix();
    const QString destination = appendingPathComponent(deletingLastPathComponent(item), destinationName);
    if (destinationName == info.fileName())
        return item;
    const bool caseOnly = destinationName.compare(info.fileName(), Qt::CaseInsensitive) == 0;
    if (!caseOnly && QFileInfo::exists(destination))
        throw FileError::nameTaken(destinationName);
    movePath(item, destination);
    return canonicalFile(destination);
}

void trash(const QString &item)
{
    if (!QFile::moveToTrash(item))
        throw FileError::failed(QStringLiteral("“%1” can’t be moved to the Recycle Bin.").arg(QFileInfo(item).fileName()));
}

bool isInside(const QString &item, const QString &folder)
{
    const QStringList parts = pathComponents(item);
    const QStringList root = pathComponents(folder);
    if (parts.size() < root.size())
        return false;
    for (qsizetype i = 0; i < root.size(); ++i)
        if (!samePathPart(parts[i], root[i]))
            return false;
    return true;
}

std::optional<QString> relocated(const QString &item, const QString &old, const QString &newPath)
{
    if (!isInside(item, old))
        return std::nullopt;
    const QStringList parts = pathComponents(item);
    const qsizetype rootCount = pathComponents(old).size();
    QString result = canonicalFile(newPath);
    for (qsizetype i = rootCount; i < parts.size(); ++i)
        result = appendingPathComponent(result, parts[i]);
    return result;
}

} // namespace wp::NoteFiles
