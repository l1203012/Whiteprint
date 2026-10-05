#pragma once
// Port of NoteWorkspace.swift: the notes Claude can reach over the bridge, and the errors it is told about.
#include "core/Note.h"

#include <QString>
#include <QStringList>
#include <exception>
#include <functional>
#include <optional>

namespace wp {

/// The notes Claude can reach over the bridge. The app's implementation (DocumentWorkspace, in
/// app/NoteDocuments.h) is backed by NoteDocuments and the notes folder; tests use an in-memory one.
/// Notes are identified by their file path.
class NoteWorkspace {
public:
    virtual ~NoteWorkspace() = default;

    /// Notes-folder files and open documents, in listing order.
    virtual QStringList noteURLs() = 0;

    /// The current content, including unsaved edits of an open document. Throws.
    virtual Note note(const QString &url) = 0;

    /// Applies `change` through the note's document, opening and showing it first if needed. The change
    /// is undoable and autosaved. Whatever `change` throws propagates and leaves the note untouched.
    virtual void edit(const QString &noteAt, const QString &actionName, const std::function<void(Note &)> &change) = 0;

    /// The note's folder relative to the notes folder (`Courses/Networks`); nullopt or empty at the
    /// top level or outside it.
    virtual std::optional<QString> folderPath(const QString &of) = 0;

    /// Saves a new note named after `title` into `folder` (relative to the notes folder, created if
    /// needed; nullopt = top level), opens it and returns its path.
    virtual QString createNote(const Note &note, const QString &title, const std::optional<QString> &folder) = 0;

    /// `edit` for a change that returns something (a new drawing id, a page number).
    template <class T, class F>
    T edit(const QString &noteAt, const QString &actionName, F &&change)
    {
        std::optional<T> result;
        edit(noteAt, actionName, std::function<void(Note &)>([&](Note &note) { result = change(note); }));
        return std::move(*result);
    }
};

class WorkspaceError : public std::exception {
public:
    enum class Kind { unknownNote, unreadable, fileNotFound, unsupportedFile, studyUnavailable, noCards, nothingDrawn };

    Kind kind() const { return m_kind; }
    /// The line Claude sees, e.g. `no note 'n9' (see list_notes)`.
    QString description() const;
    const char *what() const noexcept override { return m_what.constData(); }

    static WorkspaceError unknownNote(const QString &id) { return {Kind::unknownNote, id, {}}; }
    static WorkspaceError unreadable(const QString &name) { return {Kind::unreadable, name, {}}; }
    static WorkspaceError fileNotFound(const QString &path) { return {Kind::fileNotFound, path, {}}; }
    static WorkspaceError unsupportedFile(const QString &name) { return {Kind::unsupportedFile, name, {}}; }
    static WorkspaceError studyUnavailable() { return {Kind::studyUnavailable, {}, {}}; }
    static WorkspaceError noCards() { return {Kind::noCards, {}, {}}; }
    /// Drawing source in which nothing compiled; carries the compile errors.
    static WorkspaceError nothingDrawn(const QStringList &errors) { return {Kind::nothingDrawn, {}, errors}; }

    bool operator==(const WorkspaceError &o) const { return m_kind == o.m_kind && m_text == o.m_text && m_errors == o.m_errors; }

private:
    WorkspaceError(Kind kind, QString text, QStringList errors);
    Kind m_kind;
    QString m_text;
    QStringList m_errors;
    QByteArray m_what;
};

/// One line for Claude describing the exception being handled. Call it inside a `catch (...)`.
/// Core, study, extraction, bridge and app errors describe themselves; anything else uses `what()`.
QString currentErrorLine();

/// One line for Claude describing `error`.
QString errorLine(std::exception_ptr error);

/// `errorLine` for an error value.
template <class E>
QString errorLine(const E &error)
{
    try {
        throw error;
    } catch (...) {
        return currentErrorLine();
    }
}

} // namespace wp
