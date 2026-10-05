#pragma once
// Port of NotesLibrary.swift.
#include "app/NoteDocument.h"
#include "app/NoteEntry.h"
#include "app/NotesFolder.h"

#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <functional>

namespace wp {

/// Calls `onChange` (coalesced over a quarter second) when anything changes in a folder or any of
/// its subfolders. QFileSystemWatcher doesn't recurse, so every subfolder is watched and the set is
/// refreshed after each change.
class FolderWatcher : public QObject {
    Q_OBJECT
public:
    FolderWatcher(const QString &url, std::function<void()> onChange, QObject *parent = nullptr);

private:
    void changed();
    void refreshWatchedFolders();

    QString m_url;
    std::function<void()> m_onChange;
    QFileSystemWatcher m_watcher;
    QTimer m_timer;
};

/// The notes folder tree and the open documents, kept as one list that refreshes when files change on
/// disk or a document's title changes. Reads every note on `reload()`; `changed()` is emitted only when
/// the list or the folders actually differ.
class NotesLibrary : public QObject {
    Q_OBJECT
public:
    explicit NotesLibrary(const NotesFolder &folder = NotesFolder::current(), QObject *parent = nullptr);

    const NotesFolder &folder() const { return m_folder; }
    const QList<NoteEntry> &entries() const { return m_entries; }
    /// Every subfolder, relative to the notes folder, empty ones included.
    const QStringList &folders() const { return m_folders; }

    /// Switches to another folder (from Settings) and remembers it in `AppDefaults::store()`.
    void changeFolder(const QString &url);

    /// Notes-folder files, then open documents saved elsewhere.
    QStringList urls() const;

    static QStringList merged(const QStringList &inFolder, const QStringList &open);

    std::optional<NoteEntry> entry(const QString &forURL) const;

    void reload();

    /// Reads and parses a note file. Throws WorkspaceError::unreadable or a NoteFormatError.
    static Note read(const QString &url);

signals:
    /// The notes list or the notes folder changed (`notesLibraryDidChange`).
    void changed();

private:
    void update(NoteDocument *document);
    void startWatching();

    NotesFolder m_folder;
    QList<NoteEntry> m_entries;
    QStringList m_folders;
    FolderWatcher *m_watcher = nullptr;
};

} // namespace wp
