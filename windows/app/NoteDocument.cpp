#include "app/NoteDocument.h"

#include "app/AppEvents.h"
#include "app/AppUtil.h"
#include "app/NoteTitle.h"
#include "app/NoteWorkspace.h"
#include "app/NotesFolder.h"
#include "render/PDFExporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace wp {

NoteDocument::NoteDocument(QObject *parent) : QObject(parent)
{
    m_autosaveTimer.setSingleShot(true);
    m_autosaveTimer.setInterval(2000);
    connect(&m_autosaveTimer, &QTimer::timeout, this, [this] { autosave(); });
    m_diskTimer.setSingleShot(true);
    m_diskTimer.setInterval(150);
    connect(&m_diskTimer, &QTimer::timeout, this, [this] { checkExternalChange(); });
}

NoteDocument::~NoteDocument() = default;

NoteDocument *NoteDocument::open(const QString &path, QObject *parent)
{
    auto *document = new NoteDocument(parent);
    try {
        document->load(path);
    } catch (...) {
        delete document;
        throw;
    }
    return document;
}

Note NoteDocument::parse(const QByteArray &data, const QString &path) const
{
    const QString name = path.isEmpty() ? QStringLiteral("This note") : QFileInfo(path).fileName();
    const auto text = decodeUtf8(data);
    if (!text)
        throw DocumentError{QStringLiteral("“%1” can’t be opened.").arg(name),
                            QStringLiteral("The file isn’t valid UTF-8 text.")};
    try {
        return Note::parsing(*text);
    } catch (...) {
        throw DocumentError{QStringLiteral("“%1” can’t be opened.").arg(name),
                            capitalizedFirst(currentErrorLine()) + QLatin1Char('.')};
    }
}

void NoteDocument::load(const QString &path)
{
    const QString canonical = canonicalFile(path);
    QFile file(canonical);
    if (!file.open(QIODevice::ReadOnly))
        throw DocumentError{QStringLiteral("“%1” can’t be opened.").arg(QFileInfo(canonical).fileName()), file.errorString() + QLatin1Char('.')};
    const QByteArray data = file.readAll();
    file.close();
    const Note parsed = parse(data, canonical);

    const QString oldTitle = title();
    const QString oldPath = m_path;
    m_path = canonical;
    m_note = parsed;
    noteDiskState(data);
    m_hasConflict = false;
    m_fileMissing = false;
    m_undo.clear();
    m_redo.clear();
    m_autosaveTimer.stop();
    watchFile();
    setDirty(false);
    emit undoStateChanged();
    if (title() != oldTitle)
        emit titleChanged(title());
    if (!oldPath.isEmpty() && pathKey(oldPath) != pathKey(canonical)) {
        emit moved(oldPath);
        emit AppEvents::instance().noteDocumentDidMove(this, oldPath);
    }
    emit noteChanged(ChangeOrigin::external);
    emit AppEvents::instance().noteDocumentDidChange(this, false);
}

void NoteDocument::setFilePath(const QString &path)
{
    const QString newPath = canonicalFile(path);
    if (pathKey(newPath) == pathKey(m_path)) {
        m_path = newPath;
        return;
    }
    const QString old = m_path;
    const QString oldTitle = title();
    m_path = newPath;
    watchFile();
    if (!old.isEmpty()) {
        emit moved(old);
        emit AppEvents::instance().noteDocumentDidMove(this, old);
    }
    if (const auto renamed = NoteTitle::titleAfterRename(old, newPath);
        renamed && m_note.frontMatter.title() != renamed) {
        Note updated = m_note;
        updated.frontMatter.setTitle(renamed);
        setNote(updated, ChangeOrigin::external);
        markChanged();
    } else if (title() != oldTitle) {
        emit titleChanged(title());
    }
}

QString NoteDocument::title() const
{
    return NoteTitle::display(m_note, m_path);
}

// MARK: Changes

void NoteDocument::editorDidChange(const Note &note)
{
    if (note == m_note)
        return;
    setNote(note, ChangeOrigin::editor);
    markChanged();
}

void NoteDocument::replaceNote(const Note &note, const QString &actionName)
{
    const Note old = m_note;
    if (note == old)
        return;
    registerUndo(old, actionName);
    setNote(note, ChangeOrigin::external);
    markChanged();
}

void NoteDocument::setNote(const Note &note, ChangeOrigin origin)
{
    const QString oldTitle = title();
    m_note = note;
    if (title() != oldTitle)
        emit titleChanged(title());
    emit noteChanged(origin);
    emit AppEvents::instance().noteDocumentDidChange(this, origin == ChangeOrigin::editor);
}

void NoteDocument::markChanged()
{
    setDirty(true);
    if (!m_path.isEmpty() && m_autosaveTimer.interval() > 0)
        m_autosaveTimer.start();
}

void NoteDocument::setDirty(bool dirty)
{
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    if (!dirty)
        m_autosaveTimer.stop();
    emit dirtyChanged(dirty);
}

// MARK: Undo

void NoteDocument::registerUndo(const Note &old, const QString &actionName)
{
    switch (m_phase) {
    case UndoPhase::undoing:
        m_redo.append({old, actionName});
        break;
    case UndoPhase::redoing:
        m_undo.append({old, actionName});
        break;
    case UndoPhase::normal:
        m_undo.append({old, actionName});
        m_redo.clear();
        break;
    }
    emit undoStateChanged();
}

void NoteDocument::undo()
{
    if (m_undo.isEmpty())
        return;
    const UndoEntry entry = m_undo.takeLast();
    m_phase = UndoPhase::undoing;
    replaceNote(entry.note, entry.actionName);
    m_phase = UndoPhase::normal;
    emit undoStateChanged();
}

void NoteDocument::redo()
{
    if (m_redo.isEmpty())
        return;
    const UndoEntry entry = m_redo.takeLast();
    m_phase = UndoPhase::redoing;
    replaceNote(entry.note, entry.actionName);
    m_phase = UndoPhase::normal;
    emit undoStateChanged();
}

// MARK: Saving

void NoteDocument::setAutosaveDelay(int milliseconds)
{
    m_autosaveTimer.setInterval(qMax(0, milliseconds));
    if (milliseconds <= 0)
        m_autosaveTimer.stop();
}

void NoteDocument::noteDiskState(const QByteArray &bytes)
{
    m_diskBytes = bytes;
    const QFileInfo info(m_path);
    m_diskModified = info.lastModified();
    m_diskSize = info.size();
}

void NoteDocument::save()
{
    if (m_path.isEmpty())
        throw DocumentError{QStringLiteral("This note has no file yet."), {}};
    const QByteArray data = m_note.serialized().toUtf8();
    try {
        writeFileAtomically(m_path, data);
    } catch (const FileIOError &e) {
        throw DocumentError{e.description(), {}};
    }
    noteDiskState(data);
    m_hasConflict = false;
    m_fileMissing = false;
    watchFile();
    setDirty(false);
    emit saved();
}

void NoteDocument::saveTo(const QString &path)
{
    const QString target = canonicalFile(path);
    if (pathKey(target) != pathKey(m_path)) {
        const QByteArray data = m_note.serialized().toUtf8();
        try {
            writeFileAtomically(target, data);
        } catch (const FileIOError &e) {
            throw DocumentError{e.description(), {}};
        }
        setFilePath(target);
        noteDiskState(data);
        m_hasConflict = false;
        m_fileMissing = false;
        setDirty(false);
        emit saved();
        return;
    }
    save();
}

bool NoteDocument::autosave()
{
    if (!m_dirty || m_path.isEmpty())
        return true;
    try {
        save();
        return true;
    } catch (const DocumentError &e) {
        emit saveFailed(e.description());
        return false;
    }
}

void NoteDocument::close()
{
    m_autosaveTimer.stop();
    m_diskTimer.stop();
    if (m_dirty && !m_path.isEmpty() && !m_hasConflict && !m_fileMissing)
        autosave();
    if (m_watcher) {
        m_watcher->removePaths(m_watcher->files());
        m_watcher->removePaths(m_watcher->directories());
    }
    emit closed(this);
}

// MARK: External changes

void NoteDocument::watchFile()
{
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_diskTimer.start(); });
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_diskTimer.start(); });
    }
    const QStringList watched = m_watcher->files() + m_watcher->directories();
    const QString directory = m_path.isEmpty() ? QString() : deletingLastPathComponent(m_path);
    for (const QString &path : watched)
        if (path != m_path && path != directory)
            m_watcher->removePath(path);
    if (m_path.isEmpty())
        return;
    if (!m_watcher->files().contains(m_path) && QFileInfo::exists(m_path))
        m_watcher->addPath(m_path);
    if (!m_watcher->directories().contains(directory) && QFileInfo(directory).isDir())
        m_watcher->addPath(directory);
}

void NoteDocument::checkExternalChange()
{
    if (m_path.isEmpty())
        return;
    watchFile();
    const QFileInfo info(m_path);
    if (!info.exists()) {
        if (!m_fileMissing) {
            m_fileMissing = true;
            emit fileRemoved();
        }
        return;
    }
    m_fileMissing = false;
    if (info.lastModified() == m_diskModified && info.size() == m_diskSize)
        return;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray bytes = file.readAll();
    file.close();
    if (bytes == m_diskBytes) {
        noteDiskState(bytes);
        return;
    }
    if (m_dirty) {
        // Remember what we saw so the same change isn't reported again.
        noteDiskState(bytes);
        m_hasConflict = true;
        emit externalChangeConflict();
        return;
    }
    try {
        load(m_path);
        emit reloadedFromDisk();
    } catch (const DocumentError &e) {
        noteDiskState(bytes);
        emit reloadFailed(e.description());
    }
}

void NoteDocument::reloadFromDisk()
{
    load(m_path);
    emit reloadedFromDisk();
}

// MARK: Export

void NoteDocument::exportMarkdown(const QString &path) const
{
    writeFileAtomically(path, m_note.markdown());
}

void NoteDocument::exportPDF(const QString &path, PDFExportStyle style) const
{
    writeFileAtomically(path, PDFExporter::data(m_note, style));
}

QString NoteDocument::suggestedExportName(const QString &extension) const
{
    return NotesFolder::fileName(title()) + QLatin1Char('.') + extension;
}

void NoteDocument::showInFileManager() const
{
    if (m_path.isEmpty())
        return;
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,") + QDir::toNativeSeparators(m_path)});
}

} // namespace wp
