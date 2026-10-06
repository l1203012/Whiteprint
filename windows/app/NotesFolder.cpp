#include "app/NotesFolder.h"

#include "app/AppUtil.h"
#include "core/TextUtil.h"

#include <QCollator>
#include <QDir>
#include <QRegularExpression>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>

namespace wp {

QString NotesFolder::defaultURL()
{
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty())
        documents = QDir::homePath() + QStringLiteral("/Documents");
    return QDir::cleanPath(documents + QStringLiteral("/Whiteprint"));
}

NotesFolder NotesFolder::current(QSettings &defaults, const QProcessEnvironment &environment)
{
    const QString env = environment.value(environmentKey);
    if (!env.isEmpty())
        return NotesFolder(expandingTildeInPath(env));
    const auto chosen = AppDefaults::string(defaults, defaultsKey);
    if (chosen && !chosen->isEmpty())
        return NotesFolder(*chosen);
    return NotesFolder(defaultURL());
}

void NotesFolder::create() const
{
    if (!QDir().mkpath(url))
        throw FileIOError(QStringLiteral("The folder “%1” can’t be created.").arg(QDir::toNativeSeparators(url)));
}

// MARK: Listing

namespace {

void collect(const QString &directory, const NotesFolder &root, NotesFolder::Listing &listing)
{
    const QFileInfoList items = QDir(directory).entryInfoList(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo &item : items) {
        if (item.fileName().startsWith(QLatin1Char('.')))
            continue;
        if (item.isDir()) {
            if (const auto path = root.relativePath(item.absoluteFilePath()))
                listing.folders.append(*path);
            collect(item.absoluteFilePath(), root, listing);
        } else if (item.suffix().toLower() == NotesFolder::fileExtension) {
            listing.notes.append(canonicalFile(item.absoluteFilePath()));
        }
    }
}

} // namespace

NotesFolder::Listing NotesFolder::listing() const
{
    Listing result;
    collect(url, *this, result);
    std::stable_sort(result.folders.begin(), result.folders.end(),
                     [](const QString &a, const QString &b) { return precedes(a, b); });
    std::stable_sort(result.notes.begin(), result.notes.end(), [this](const QString &a, const QString &b) {
        const QString fa = relativeFolder(a).value_or(QString());
        const QString fb = relativeFolder(b).value_or(QString());
        if (fa != fb)
            return precedes(fa, fb);
        return precedes(QFileInfo(a).fileName(), QFileInfo(b).fileName());
    });
    return result;
}

QStringList NotesFolder::noteURLs() const
{
    return listing().notes;
}

bool NotesFolder::contains(const QString &file) const
{
    const auto path = relativePath(file);
    return path && !path->isEmpty();
}

std::optional<QString> NotesFolder::relativePath(const QString &item) const
{
    const QStringList root = pathComponents(url);
    const QStringList parts = pathComponents(item);
    if (parts.size() < root.size())
        return std::nullopt;
    for (qsizetype i = 0; i < root.size(); ++i)
        if (!samePathPart(parts[i], root[i]))
            return std::nullopt;
    return parts.mid(root.size()).join(QLatin1Char('/'));
}

std::optional<QString> NotesFolder::relativeFolder(const QString &file) const
{
    return relativePath(deletingLastPathComponent(canonicalFile(file)));
}

bool NotesFolder::precedes(const QString &a, const QString &b)
{
    static const QCollator collator = [] {
        QCollator c;
        c.setNumericMode(true);
        c.setCaseSensitivity(Qt::CaseInsensitive);
        return c;
    }();
    const int result = collator.compare(a, b);
    return result != 0 ? result < 0 : a < b;
}

// MARK: Subfolders

QString NotesFolder::folderURL(const QString &path) const
{
    const QString trimmed = trimmedWhitespace(path);
    if (trimmed.startsWith(QLatin1Char('/')) || trimmed.startsWith(QLatin1Char('\\')) || trimmed.startsWith(QLatin1Char('~')))
        throw FolderError{path};
    QStringList parts;
    for (const QString &raw : trimmed.split(QRegularExpression(QStringLiteral("[/\\\\]")), Qt::SkipEmptyParts)) {
        const QString part = trimmedWhitespace(raw);
        if (!part.isEmpty())
            parts.append(part);
    }
    for (const QString &part : parts)
        if (part == QLatin1String("..") || part == QLatin1String(".") || part.contains(QLatin1Char(':')))
            throw FolderError{path};
    QString result = canonicalFile(url);
    for (const QString &part : parts)
        result = appendingPathComponent(result, part);
    return result;
}

QString NotesFolder::createFolder(const QString &name, const QString &parent) const
{
    const QString directory = parent.isEmpty() ? url : parent;
    const QString folder = unusedURL(fileName(name), std::nullopt, directory);
    if (!QDir().mkpath(folder))
        throw FileIOError(QStringLiteral("The folder “%1” can’t be created.").arg(QDir::toNativeSeparators(folder)));
    return canonicalFile(folder);
}

// MARK: New notes

QString NotesFolder::unusedURL(const QString &forTitle, const QString &folder) const
{
    return canonicalFile(unusedURL(fileName(forTitle), fileExtension, folder.isEmpty() ? url : folder));
}

QString NotesFolder::unusedURL(const QString &base, const std::optional<QString> &extension, const QString &directory)
{
    const auto candidate = [&](const QString &name) {
        return appendingPathComponent(directory, extension ? name + QLatin1Char('.') + *extension : name);
    };
    QString result = candidate(base);
    int n = 2;
    while (QFileInfo::exists(result)) {
        result = candidate(QStringLiteral("%1 %2").arg(base).arg(n));
        ++n;
    }
    return result;
}

QString NotesFolder::fileName(const QString &title)
{
    static const QString forbidden = QStringLiteral("/:\\<>\"|?*");
    QString cleaned;
    cleaned.reserve(title.size());
    for (const QChar c : title) {
        const bool replace = forbidden.contains(c) || isNewlineChar(c) || c.category() == QChar::Other_Control;
        cleaned += replace ? QChar(u'-') : c;
    }
    cleaned = trimmedWhitespace(cleaned);
    qsizetype dots = 0;
    while (dots < cleaned.size() && cleaned.at(dots) == QLatin1Char('.'))
        ++dots;
    QString trimmed = trimmedWhitespace(prefixGraphemes(cleaned.mid(dots), 100));
    // Windows drops trailing dots and spaces from names.
    while (trimmed.endsWith(QLatin1Char('.')))
        trimmed = trimmedWhitespace(trimmed.chopped(1));
    return trimmed.isEmpty() ? QStringLiteral("Untitled") : trimmed;
}

QString NotesFolder::save(const Note &note, const QString &title, const QString &folder) const
{
    const QString directory = folder.isEmpty() ? url : folder;
    if (!QDir().mkpath(directory))
        throw FileIOError(QStringLiteral("The folder “%1” can’t be created.").arg(QDir::toNativeSeparators(directory)));
    const QString file = unusedURL(title, folder);
    writeFileAtomically(file, note.serialized());
    return file;
}

} // namespace wp
