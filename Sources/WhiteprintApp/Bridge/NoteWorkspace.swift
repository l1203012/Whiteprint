import Foundation
import WhiteprintCore

/// The notes Claude can reach over the bridge. The app's implementation is
/// backed by NSDocuments and the notes folder; tests use an in-memory one.
protocol NoteWorkspace: AnyObject {
    /// Notes-folder files and open documents, in listing order.
    func noteURLs() -> [URL]

    /// The current content, including unsaved edits of an open document.
    func note(at url: URL) throws -> Note

    /// Applies `change` through the note's document, opening and showing it
    /// first if needed. The change is undoable and autosaved.
    func edit<T>(noteAt url: URL, actionName: String, _ change: (inout Note) throws -> T) throws -> T

    /// The note's folder relative to the notes folder (`Courses/Networks`);
    /// nil or empty at the top level or outside it.
    func folderPath(of url: URL) -> String?

    /// Saves a new note named after `title` into `folder` (relative to the notes
    /// folder, created if needed; nil = top level), opens it and returns its URL.
    func createNote(_ note: Note, title: String, folder: String?) throws -> URL
}

enum WorkspaceError: Error, CustomStringConvertible {
    case unknownNote(String)
    case unreadable(String)
    case fileNotFound(String)
    case unsupportedFile(String)
    case studyUnavailable
    case noCards
    /// Drawing source in which nothing compiled; carries the compile errors.
    case nothingDrawn([String])

    var description: String {
        switch self {
        case .unknownNote(let id): return "no note '\(id)' (see list_notes)"
        case .unreadable(let name): return "\(name) can't be read"
        case .fileNotFound(let path): return "no file at \(path)"
        case .unsupportedFile(let name): return "\(name): unsupported file type (use PDF, DOCX, DOC or PPTX)"
        case .studyUnavailable: return "the study store couldn't be opened"
        case .noCards: return "no cards given; each card needs a question and an answer"
        case .nothingDrawn(let errors): return (["nothing to draw, not saved:"] + errors).joined(separator: "\n")
        }
    }
}

/// One line for Claude describing `error`. Core, study and extraction errors
/// describe themselves; Cocoa errors use their localized description.
func errorLine(_ error: Error) -> String {
    switch error {
    case NoteFormatError.unsupportedVersion(let version):
        return "note uses format \(version); this Whiteprint reads up to \(Note.formatVersion)"
    case NoteFormatError.invalidVersion(let raw):
        return "note has an invalid format version '\(raw)'"
    case let error as CustomStringConvertible where !(type(of: error) is NSError.Type):
        return error.description
    default:
        return error.localizedDescription
    }
}
