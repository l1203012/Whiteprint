#pragma once
// Port of NoteDocuments.swift: finding and opening note documents. NSDocumentController becomes a
// process-wide QObject that owns the open NoteDocuments; the UI opens a window for each one it announces.
#include "app/NoteDocument.h"
#include "app/NoteWorkspace.h"

#include <QList>
#include <QObject>
#include <QString>

namespace wp {

class NotesLibrary;

/// The open note documents. The UI connects to `documentAdded` / `showRequested` to create and raise
/// windows, and calls `close` when a window closes.
class NoteDocuments : public QObject {
    Q_OBJECT
public:
    static NoteDocuments &instance();

    /// Every open document, in the order they were opened.
    QList<NoteDocument *> openDocuments() const { return m_documents; }

    /// The open document for the file, if any (paths are compared canonically).
    NoteDocument *document(const QString &url) const;

    /// Opens `url` synchronously (so a bridge request can edit it right away), announces new documents
    /// with `documentAdded`, and asks the UI to bring its window to the front with `showRequested`.
    /// Throws DocumentError.
    NoteDocument *show(const QString &url);

    /// Opens `url` and shows it, reporting failures through `errorOccurred` instead of throwing (the
    /// usual `openDocument` for menus and the sidebar).
    void open(const QString &url);

    /// Hands over a document the UI made itself (a new, untitled note); it is announced with `documentAdded`.
    void adopt(NoteDocument *document);

    /// Saves pending changes, announces `documentClosed` and deletes the document.
    void close(NoteDocument *document);

    /// Open documents whose file is `item` or inside it (when it's a folder).
    QList<NoteDocument *> documents(const QString &at) const;

    /// Points open documents at their new location after `old` (a note or a folder) moved to `newPath`.
    void relocate(const QString &old, const QString &newPath);

signals:
    /// A document was opened or adopted: create its window.
    void documentAdded(wp::NoteDocument *document);
    /// The document is being closed and deleted.
    void documentClosed(wp::NoteDocument *document);
    /// Show (and activate) the window of this document.
    void showRequested(wp::NoteDocument *document);
    /// A document couldn't be opened by `open`; the text is ready for a message box.
    void errorOccurred(const QString &message);

private:
    NoteDocuments() = default;
    QList<NoteDocument *> m_documents;
};

/// The app's `NoteWorkspace`: the notes library plus open documents.
class DocumentWorkspace : public NoteWorkspace {
public:
    explicit DocumentWorkspace(NotesLibrary *library) : m_library(library) {}

    QStringList noteURLs() override;
    Note note(const QString &url) override;
    void edit(const QString &noteAt, const QString &actionName, const std::function<void(Note &)> &change) override;
    std::optional<QString> folderPath(const QString &of) override;
    QString createNote(const Note &note, const QString &title, const std::optional<QString> &folder) override;

private:
    NotesLibrary *m_library;
};

} // namespace wp
