#pragma once
// Port of NoteDocument.swift. NSDocument becomes a plain QObject: it loads and saves one `.wprint`
// file (atomically), tracks dirty state, keeps an undo stack for edits made outside the editor,
// autosaves in place, and notices when the file changes on disk.
#include "core/Note.h"
#include "render/Palette.h"

#include <QByteArray>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <exception>
#include <functional>
#include <type_traits>

namespace wp {

/// A note file that couldn't be opened; `message` is the headline, `suggestion` the detail.
struct DocumentError {
    QString message;
    QString suggestion;
    QString description() const { return suggestion.isEmpty() ? message : message + QLatin1Char(' ') + suggestion; }
};

/// A `.wprint` file open in the app. One window per note; autosaves in place.
///
/// Observers: connect to the signals below, or to the global `AppEvents::noteDocumentDidChange` /
/// `noteDocumentDidMove` (emitted for every document). Everything runs on the GUI thread.
///
/// Editing paths:
///  - `editorDidChange(note)`: a user edit reported by the editor. The editor keeps its own text undo
///    stack, so nothing is registered here. Marks the document dirty and schedules an autosave.
///  - `apply(actionName, change)`: an edit made outside the editor (Claude, menus). Undoable through
///    `undo()`/`redo()`, shown in the editor straight away (`noteChanged(external)`) and autosaved at once.
class NoteDocument : public QObject {
    Q_OBJECT
public:
    static inline const QString typeName = QStringLiteral("io.github.l1203012.whiteprint.note");

    /// Where a change came from, so the editor isn't sent back its own edits.
    enum class ChangeOrigin { editor, external };
    Q_ENUM(ChangeOrigin)

    /// An untitled, empty note without a file; give it one with `saveTo`.
    explicit NoteDocument(QObject *parent = nullptr);
    ~NoteDocument() override;

    /// Reads `path` into a new document. Throws DocumentError.
    static NoteDocument *open(const QString &path, QObject *parent = nullptr);

    /// (Re)reads the file, replacing the note, dropping unsaved changes and the undo history. Throws DocumentError.
    void load(const QString &path);

    const Note &note() const { return m_note; }

    /// The canonical file path; empty for a note that was never saved.
    QString filePath() const { return m_path; }
    /// Points the document at another file (a rename or move). Emits `moved`; a changed file name
    /// becomes the note's title (`NoteTitle::titleAfterRename`) and the document is marked dirty.
    void setFilePath(const QString &path);

    /// `NoteTitle::display(note, filePath)`.
    QString title() const;

    bool isDirty() const { return m_dirty; }

    // MARK: Changes

    /// A user edit reported by the editor.
    void editorDidChange(const Note &note);

    /// Applies `change` to a copy of the note; when it doesn't throw, the copy replaces the note
    /// (undoable under `actionName`) and is autosaved. Returns what `change` returns.
    template <class F>
    auto apply(const QString &actionName, F &&change) -> std::invoke_result_t<F, Note &>
    {
        Note edited = m_note;
        if constexpr (std::is_void_v<std::invoke_result_t<F, Note &>>) {
            change(edited);
            replaceNote(edited, actionName);
            autosave();
        } else {
            auto result = change(edited);
            replaceNote(edited, actionName);
            autosave();
            return result;
        }
    }

    // MARK: Undo (edits made through `apply`)

    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    /// The name given to `apply` for the edit `undo()` would revert; empty when there's none.
    QString undoActionName() const { return m_undo.isEmpty() ? QString() : m_undo.last().actionName; }
    QString redoActionName() const { return m_redo.isEmpty() ? QString() : m_redo.last().actionName; }
    void undo();
    void redo();

    // MARK: Saving

    /// Delay after the last editor change before the document saves itself; 0 disables the timer.
    int autosaveDelay() const { return m_autosaveTimer.interval(); }
    void setAutosaveDelay(int milliseconds);

    /// Writes the note to its file atomically. Throws DocumentError when there is no file or the
    /// write fails.
    void save();
    /// Writes the note to `path` (Save As); the document then lives there. Throws DocumentError.
    void saveTo(const QString &path);
    /// Saves when dirty and a file exists; failures are reported through `saveFailed`. Returns success.
    bool autosave();

    /// Saves pending changes and tells observers the document is gone; call before dropping it
    /// (NoteDocuments::close does).
    void close();

    // MARK: External changes

    /// True while the file on disk changed behind unsaved edits. `save()` or `load()` resolves it.
    bool hasExternalConflict() const { return m_hasConflict; }
    /// Re-reads the file now, discarding unsaved edits; the same as `load(filePath())`.
    void reloadFromDisk();
    /// Looks at the file now instead of waiting for the file system watcher (it calls this itself).
    void checkExternalChange();

    // MARK: Export

    /// Plain Markdown (`Note::markdown`). Throws FileIOError.
    void exportMarkdown(const QString &path) const;
    /// Typeset PDF, `blueprint` or `print` style. Throws FileIOError.
    void exportPDF(const QString &path, PDFExportStyle style) const;
    /// The suggested file name for an export: the title made safe, plus `.ext`.
    QString suggestedExportName(const QString &extension) const;

    /// Opens Explorer with the file selected. No-op without a file.
    void showInFileManager() const;

signals:
    /// The note changed, from the editor, Claude, undo/redo or a reload.
    void noteChanged(wp::NoteDocument::ChangeOrigin origin);
    /// The displayed title changed (front matter edit or rename).
    void titleChanged(const QString &title);
    void dirtyChanged(bool dirty);
    /// The file was renamed or moved; `oldPath` is where it was.
    void moved(const QString &oldPath);
    /// canUndo/canRedo/action names may have changed.
    void undoStateChanged();
    void saved();
    void saveFailed(const QString &message);
    /// The file changed on disk and the document has no unsaved edits: it was reloaded (the note changed
    /// with origin `external`).
    void reloadedFromDisk();
    /// The file changed on disk but there are unsaved edits; nothing was reloaded. Resolve with
    /// `reloadFromDisk()` (take the disk version) or `save()` (keep ours).
    void externalChangeConflict();
    /// The file changed on disk and couldn't be read as a note; the message is for the user.
    void reloadFailed(const QString &message);
    /// The file was deleted or renamed outside the app.
    void fileRemoved();
    /// Emitted by `close()`.
    void closed(wp::NoteDocument *document);

private:
    struct UndoEntry {
        Note note;
        QString actionName;
    };
    enum class UndoPhase { normal, undoing, redoing };

    void replaceNote(const Note &note, const QString &actionName);
    void setNote(const Note &note, ChangeOrigin origin);
    void markChanged();
    void setDirty(bool dirty);
    void registerUndo(const Note &old, const QString &actionName);
    void watchFile();
    void noteDiskState(const QByteArray &bytes);
    Note parse(const QByteArray &data, const QString &path) const;

    Note m_note;
    QString m_path;
    bool m_dirty = false;
    QList<UndoEntry> m_undo;
    QList<UndoEntry> m_redo;
    UndoPhase m_phase = UndoPhase::normal;
    QTimer m_autosaveTimer;
    QTimer m_diskTimer;
    QFileSystemWatcher *m_watcher = nullptr;
    QByteArray m_diskBytes;
    QDateTime m_diskModified;
    qint64 m_diskSize = -1;
    bool m_hasConflict = false;
    bool m_fileMissing = false;
};

} // namespace wp
