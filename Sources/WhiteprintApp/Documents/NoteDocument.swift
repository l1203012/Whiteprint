import AppKit
import UniformTypeIdentifiers
import WhiteprintCore
import WhiteprintRender

extension Notification.Name {
    /// Posted when a document's note changes, from either the editor or Claude.
    static let noteDocumentDidChange = Notification.Name("WhiteprintNoteDocumentDidChange")
    /// Posted when a document's file is renamed or moved; `userInfo["old"]` is the previous URL.
    static let noteDocumentDidMove = Notification.Name("WhiteprintNoteDocumentDidMove")
}

/// A `.wprint` file. One window per note; autosaves in place.
@objc(NoteDocument)
final class NoteDocument: NSDocument {
    static let typeName = "io.github.l1203012.whiteprint.note"

    private(set) var note = Note()

    /// Where a change came from, so the editor isn't sent back its own edits.
    enum ChangeOrigin {
        case editor, external
    }

    override class var autosavesInPlace: Bool { true }

    override var fileURL: URL? {
        didSet {
            guard oldValue?.canonicalFile != fileURL?.canonicalFile else { return }
            if let old = oldValue {
                NotificationCenter.default.post(name: .noteDocumentDidMove, object: self, userInfo: ["old": old])
            }
            if let title = NoteTitle.titleAfterRename(from: oldValue, to: fileURL), note.frontMatter.title != title {
                var renamed = note
                renamed.frontMatter.title = title
                setNote(renamed, origin: .external)
                // Not during the rename itself: NSDocument is still finishing it.
                DispatchQueue.main.async { self.updateChangeCount(.changeDone) }
            }
        }
    }

    var title: String {
        NoteTitle.display(for: note, fileURL: fileURL)
    }

    /// The title, after the note's icon when icons are shown: the window
    /// and tab title.
    override var displayName: String! {
        get {
            guard ViewPreferences.shared.showsCoversAndIcons, let icon = note.frontMatter.icon else { return title }
            return icon + " " + title
        }
        set { super.displayName = newValue }
    }

    private var preferencesObserver: NSObjectProtocol?

    override func makeWindowControllers() {
        addWindowController(NoteWindowController(document: self))
        // Showing or hiding icons changes the window title.
        preferencesObserver = NotificationCenter.default.addObserver(
            forName: .viewPreferencesDidChange, object: nil, queue: .main
        ) { [weak self] _ in
            self?.windowControllers.forEach { $0.synchronizeWindowTitleWithDocumentName() }
        }
    }

    deinit {
        preferencesObserver.map(NotificationCenter.default.removeObserver)
    }

    override func data(ofType typeName: String) throws -> Data {
        Data(note.serialized().utf8)
    }

    override func read(from data: Data, ofType typeName: String) throws {
        guard let text = String(data: data, encoding: .utf8) else {
            throw CocoaError(.fileReadInapplicableStringEncoding)
        }
        do {
            note = try Note(parsing: text)
        } catch {
            throw NSError(domain: NSCocoaErrorDomain, code: CocoaError.fileReadCorruptFile.rawValue, userInfo: [
                NSLocalizedDescriptionKey: "“\(fileURL?.lastPathComponent ?? "This note")” can’t be opened.",
                NSLocalizedRecoverySuggestionErrorKey: errorLine(error).capitalizedFirst + ".",
            ])
        }
    }

    // MARK: Changes

    /// A user edit reported by the editor. The text view keeps its own undo stack.
    func editorDidChange(_ note: Note) {
        guard note != self.note else { return }
        setNote(note, origin: .editor)
        updateChangeCount(.changeDone)
    }

    /// An edit made outside the editor (Claude, the menus): undoable, shown
    /// in the editor straight away, and autosaved.
    @discardableResult
    func apply<T>(actionName: String, _ change: (inout Note) throws -> T) rethrows -> T {
        var edited = note
        let result = try change(&edited)
        replaceNote(with: edited, actionName: actionName)
        autosave(withImplicitCancellability: false) { _ in }
        return result
    }

    private func replaceNote(with new: Note, actionName: String) {
        let old = note
        guard new != old else { return }
        undoManager?.registerUndo(withTarget: self) { document in
            document.replaceNote(with: old, actionName: actionName)
        }
        undoManager?.setActionName(actionName)
        setNote(new, origin: .external)
        updateChangeCount(.changeDone)
    }

    private func setNote(_ new: Note, origin: ChangeOrigin) {
        let oldName = displayName
        note = new
        if displayName != oldName {
            windowControllers.forEach { $0.synchronizeWindowTitleWithDocumentName() }
        }
        NotificationCenter.default.post(name: .noteDocumentDidChange, object: self, userInfo: ["origin": origin])
    }

    // MARK: Export

    @IBAction func exportMarkdown(_ sender: Any?) {
        export(type: UTType(filenameExtension: "md") ?? .plainText, extension: "md") { [note] in
            Data(note.markdown().utf8)
        }
    }

    @IBAction func exportBlueprintPDF(_ sender: Any?) {
        export(type: .pdf, extension: "pdf") { [note] in PDFExporter.data(for: note, style: .blueprint) }
    }

    @IBAction func exportPrintPDF(_ sender: Any?) {
        export(type: .pdf, extension: "pdf") { [note] in PDFExporter.data(for: note, style: .print) }
    }

    private func export(type: UTType, extension ext: String, data: @escaping () -> Data) {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [type]
        panel.nameFieldStringValue = NotesFolder.fileName(forTitle: title) + "." + ext
        panel.directoryURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first
        let write: (NSApplication.ModalResponse) -> Void = { response in
            guard response == .OK, let url = panel.url else { return }
            do {
                try data().write(to: url, options: .atomic)
            } catch {
                NSApp.presentError(error)
            }
        }
        if let window = windowForSheet {
            panel.beginSheetModal(for: window, completionHandler: write)
        } else {
            write(panel.runModal())
        }
    }

    @IBAction func showInFinder(_ sender: Any?) {
        guard let fileURL else { return }
        NSWorkspace.shared.activateFileViewerSelecting([fileURL])
    }

    override func validateUserInterfaceItem(_ item: NSValidatedUserInterfaceItem) -> Bool {
        if item.action == #selector(showInFinder(_:)) { return fileURL != nil }
        return super.validateUserInterfaceItem(item)
    }
}

extension String {
    var capitalizedFirst: String {
        prefix(1).uppercased() + dropFirst()
    }
}
