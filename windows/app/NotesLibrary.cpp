#include "app/NotesLibrary.h"

#include "app/AppEvents.h"
#include "app/AppUtil.h"
#include "app/NoteDocuments.h"
#include "app/NoteTitle.h"
#include "app/NoteWorkspace.h"

#include <QDir>
#include <QFileInfo>

namespace wp {

// MARK: FolderWatcher

FolderWatcher::FolderWatcher(const QString &url, std::function<void()> onChange, QObject *parent)
    : QObject(parent), m_url(url), m_onChange(std::move(onChange))
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        refreshWatchedFolders();
        if (m_onChange)
            m_onChange();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { changed(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { changed(); });
    refreshWatchedFolders();
}

void FolderWatcher::changed()
{
    m_timer.start();
}

void FolderWatcher::refreshWatchedFolders()
{
    constexpr int limit = 400;
    QStringList directories;
    QList<QString> pending{m_url};
    while (!pending.isEmpty() && directories.size() < limit) {
        const QString directory = pending.takeLast();
        if (!QFileInfo(directory).isDir())
            continue;
        directories.append(directory);
        const QFileInfoList children = QDir(directory).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        for (const QFileInfo &child : children)
            if (!child.fileName().startsWith(QLatin1Char('.')))
                pending.append(child.absoluteFilePath());
    }
    const QStringList watched = m_watcher.directories();
    QStringList stale;
    for (const QString &path : watched)
        if (!directories.contains(path))
            stale.append(path);
    if (!stale.isEmpty())
        m_watcher.removePaths(stale);
    QStringList fresh;
    for (const QString &path : directories)
        if (!watched.contains(path))
            fresh.append(path);
    if (!fresh.isEmpty())
        m_watcher.addPaths(fresh);
}

// MARK: NotesLibrary

NotesLibrary::NotesLibrary(const NotesFolder &folder, QObject *parent) : QObject(parent), m_folder(folder)
{
    connect(&AppEvents::instance(), &AppEvents::noteDocumentDidChange, this, [this](NoteDocument *document, bool) {
        if (document)
            update(document);
    });
    connect(&AppEvents::instance(), &AppEvents::noteDocumentDidMove, this, [this](NoteDocument *, const QString &) { reload(); });
    startWatching();
    reload();
}

void NotesLibrary::changeFolder(const QString &url)
{
    AppDefaults::store().setValue(NotesFolder::defaultsKey, url);
    m_folder = NotesFolder(url);
    startWatching();
    reload();
}

QStringList NotesLibrary::urls() const
{
    QStringList open;
    for (NoteDocument *document : NoteDocuments::instance().openDocuments())
        if (!document->filePath().isEmpty())
            open.append(canonicalFile(document->filePath()));
    return merged(m_folder.noteURLs(), open);
}

QStringList NotesLibrary::merged(const QStringList &inFolder, const QStringList &open)
{
    QSet<QString> known;
    for (const QString &path : inFolder)
        known.insert(pathKey(path));
    QStringList result = inFolder;
    for (const QString &path : open)
        if (!known.contains(pathKey(path)))
            result.append(path);
    return result;
}

std::optional<NoteEntry> NotesLibrary::entry(const QString &forURL) const
{
    const QString key = pathKey(forURL);
    for (const NoteEntry &entry : m_entries)
        if (pathKey(entry.url) == key)
            return entry;
    return std::nullopt;
}

void NotesLibrary::reload()
{
    const NotesFolder::Listing listing = m_folder.listing();
    QStringList open;
    for (NoteDocument *document : NoteDocuments::instance().openDocuments())
        if (!document->filePath().isEmpty())
            open.append(canonicalFile(document->filePath()));
    const QStringList all = merged(listing.notes, open);

    QList<NoteEntry> fresh;
    fresh.reserve(all.size());
    for (const QString &url : all) {
        std::optional<Note> note;
        if (NoteDocument *document = NoteDocuments::instance().document(url)) {
            note = document->note();
        } else {
            try {
                note = read(url);
            } catch (...) {
            }
        }
        NoteEntry entry;
        entry.url = url;
        entry.title = note ? NoteTitle::display(*note, url) : NoteTitle::baseName(url);
        entry.pageCount = note ? int(note->pages().size()) : 0;
        entry.folder = m_folder.relativeFolder(url);
        if (note)
            entry.decks = note->decks();
        fresh.append(entry);
    }
    if (fresh == m_entries && listing.folders == m_folders)
        return;
    m_entries = fresh;
    m_folders = listing.folders;
    emit changed();
}

void NotesLibrary::update(NoteDocument *document)
{
    if (document->filePath().isEmpty())
        return reload();
    const QString key = pathKey(document->filePath());
    int index = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (pathKey(m_entries[i].url) == key) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return reload();
    NoteEntry entry = m_entries[index];
    entry.title = document->title();
    entry.pageCount = int(document->note().pages().size());
    entry.decks = document->note().decks();
    if (m_entries[index] == entry)
        return;
    m_entries[index] = entry;
    emit changed();
}

Note NotesLibrary::read(const QString &url)
{
    const auto text = readUtf8File(url);
    if (!text)
        throw WorkspaceError::unreadable(QFileInfo(url).fileName());
    return Note::parsing(*text);
}

void NotesLibrary::startWatching()
{
    try {
        m_folder.create();
    } catch (...) {
    }
    delete m_watcher;
    m_watcher = new FolderWatcher(m_folder.url, [this] { reload(); }, this);
}

} // namespace wp
