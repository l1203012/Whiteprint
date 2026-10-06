#pragma once
// Port of NotesFolder.swift. Files and folders are canonical-able path strings (see app/AppUtil.h).
#include "app/AppDefaults.h"
#include "core/Note.h"

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <exception>
#include <optional>

namespace wp {

/// The folder the sidebar lists and new notes are saved to. It's a tree: notes may sit in
/// subfolders at any depth.
struct NotesFolder {
    static inline const QString fileExtension = QStringLiteral("wprint");
    static inline const QString defaultsKey = QStringLiteral("NotesFolderPath");
    static inline const QString environmentKey = QStringLiteral("WHITEPRINT_NOTES_DIR");

    /// The folder's path.
    QString url;

    NotesFolder() = default;
    explicit NotesFolder(QString url) : url(std::move(url)) {}
    bool operator==(const NotesFolder &) const = default;

    /// `Documents\Whiteprint` (QStandardPaths::DocumentsLocation).
    static QString defaultURL();

    /// The `WHITEPRINT_NOTES_DIR` environment variable wins (handy for testing), then the folder
    /// chosen in Settings (`defaults`, key `NotesFolderPath`), then the default.
    static NotesFolder current(QSettings &defaults = AppDefaults::store(),
                               const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment());

    /// Creates the folder (and parents). Throws FileIOError.
    void create() const;

    // MARK: Listing

    /// Everything under the folder: subfolders as relative paths, and notes (canonical paths).
    struct Listing {
        QStringList folders;
        QStringList notes;
        bool operator==(const Listing &) const = default;
    };

    /// Subfolders and `.wprint` files at any depth, hidden items skipped. Notes are sorted by folder,
    /// top level first, then by name as Explorer does (numbers in order, case-insensitive).
    Listing listing() const;

    /// `.wprint` files at any depth, in listing order.
    QStringList noteURLs() const;

    /// Whether `file` is anywhere inside the folder (the folder itself doesn't count).
    bool contains(const QString &file) const;

    /// `Courses/Networks` for `<root>/Courses/Networks`, `""` for the root itself, nullopt for
    /// anything outside.
    std::optional<QString> relativePath(const QString &item) const;

    /// The folder a note is in, relative to the root (`""` at the top level), or nullopt when the
    /// note is saved elsewhere.
    std::optional<QString> relativeFolder(const QString &file) const;

    /// Explorer-style ordering: case-insensitive, digits compared as numbers.
    static bool precedes(const QString &a, const QString &b);

    // MARK: Subfolders

    struct FolderError {
        QString path;
        QString description() const
        {
            return QStringLiteral("folder '%1' must be a path inside the notes folder, like Courses/Networks (no '..' or leading '/')")
                .arg(path);
        }
    };

    /// The path of the subfolder at `path` (`Courses/Networks`). Only relative paths that stay inside
    /// the folder are accepted; empty means the root. Throws FolderError.
    QString folderURL(const QString &path) const;

    /// Creates `New Folder` (or `New Folder 2`, ...) inside `parent` (default: the root).
    QString createFolder(const QString &name = QStringLiteral("New Folder"), const QString &parent = {}) const;

    // MARK: New notes

    /// A file path for a new note named after `title` that doesn't exist yet: `Untitled.wprint`, then
    /// `Untitled 2.wprint`, ...
    QString unusedURL(const QString &forTitle, const QString &folder = {}) const;

    /// `<base>.<ext>` in `directory`, counting up while the name is taken (`ext` nullopt: a folder name).
    static QString unusedURL(const QString &base, const std::optional<QString> &extension, const QString &directory);

    /// A safe file name for `title`: no path separators, colons, other characters Windows forbids or
    /// leading dots, at most 100 characters, `Untitled` when nothing is left.
    static QString fileName(const QString &forTitle);

    /// Writes `note` to a new file named after `title` in `folder` (default: the root, created if
    /// needed) and returns its path. Throws FileIOError.
    QString save(const Note &note, const QString &title, const QString &folder = {}) const;
};

} // namespace wp
