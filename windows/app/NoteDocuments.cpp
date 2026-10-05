#include "app/NoteDocuments.h"

#include "app/AppUtil.h"
#include "app/NoteFiles.h"
#include "app/NotesFolder.h"
#include "app/NotesLibrary.h"

#include <QFileInfo>

namespace wp {

NoteDocuments &NoteDocuments::instance()
{
    static NoteDocuments documents;
    return documents;
}

NoteDocument *NoteDocuments::document(const QString &url) const
{
    const QString key = pathKey(url);
    for (NoteDocument *document : m_documents)
        if (!document->filePath().isEmpty() && pathKey(document->filePath()) == key)
            return document;
    return nullptr;
}

NoteDocument *NoteDocuments::show(const QString &url)
{
    NoteDocument *document = this->document(url);
    if (!document) {
        document = NoteDocument::open(url, this);
        m_documents.append(document);
        emit documentAdded(document);
    }
    emit showRequested(document);
    return document;
}

void NoteDocuments::open(const QString &url)
{
    try {
        show(url);
    } catch (...) {
        emit errorOccurred(currentErrorLine());
    }
}

void NoteDocuments::adopt(NoteDocument *document)
{
    if (!document || m_documents.contains(document))
        return;
    document->setParent(this);
    m_documents.append(document);
    emit documentAdded(document);
}

void NoteDocuments::close(NoteDocument *document)
{
    if (!m_documents.contains(document))
        return;
    document->close();
    m_documents.removeAll(document);
    emit documentClosed(document);
    document->deleteLater();
}

QList<NoteDocument *> NoteDocuments::documents(const QString &at) const
{
    QList<NoteDocument *> result;
    for (NoteDocument *document : m_documents)
        if (!document->filePath().isEmpty() && NoteFiles::isInside(document->filePath(), at))
            result.append(document);
    return result;
}

void NoteDocuments::relocate(const QString &old, const QString &newPath)
{
    for (NoteDocument *document : documents(old)) {
        const auto moved = NoteFiles::relocated(document->filePath(), old, newPath);
        if (moved)
            document->setFilePath(*moved);
    }
}

// MARK: DocumentWorkspace

QStringList DocumentWorkspace::noteURLs()
{
    return m_library->urls();
}

Note DocumentWorkspace::note(const QString &url)
{
    if (NoteDocument *document = NoteDocuments::instance().document(url))
        return document->note();
    if (!QFileInfo::exists(url))
        throw WorkspaceError::fileNotFound(url);
    return NotesLibrary::read(url);
}

void DocumentWorkspace::edit(const QString &noteAt, const QString &actionName, const std::function<void(Note &)> &change)
{
    auto &documents = NoteDocuments::instance();
    if (!documents.document(noteAt) && !QFileInfo::exists(noteAt))
        throw WorkspaceError::fileNotFound(noteAt);
    documents.show(noteAt)->apply(actionName, change);
}

std::optional<QString> DocumentWorkspace::folderPath(const QString &of)
{
    return m_library->folder().relativeFolder(of);
}

QString DocumentWorkspace::createNote(const Note &note, const QString &title, const std::optional<QString> &folder)
{
    std::optional<QString> directory;
    if (folder)
        directory = m_library->folder().folderURL(*folder);
    const QString url = m_library->folder().save(note, title, directory.value_or(QString()));
    NoteDocuments::instance().show(url);
    m_library->reload();
    return url;
}

} // namespace wp
