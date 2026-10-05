import Foundation
import WhiteprintStudy

/// The app-wide objects shared by every window and the bridge.
final class AppServices {
    static let shared = AppServices()

    let library: NotesLibrary
    let registry = NoteRegistry()
    /// Nil when the study folder can't be created; study features then say so.
    let studyStore: StudyStore?
    let study: StudySession
    let bridge: BridgeService
    let flashcards = FlashcardProgressStore()

    private init() {
        library = NotesLibrary()
        studyStore = try? StudyStore()
        bridge = BridgeService(workspace: DocumentWorkspace(library: library), registry: registry, study: studyStore)
        study = StudySession(store: studyStore, tools: bridge)
        NotificationCenter.default.addObserver(forName: .noteDocumentDidMove, object: nil, queue: .main) { [registry, flashcards] notification in
            guard let old = notification.userInfo?["old"] as? URL,
                  let new = (notification.object as? NoteDocument)?.fileURL else { return }
            registry.move(from: old, to: new)
            flashcards.moveNotes(from: old, to: new)
        }
    }

    /// Keeps documents, Claude's note ids and flashcard progress with a note
    /// or folder that moved from `old` to `new` on disk.
    func itemDidMove(from old: URL, to new: URL) {
        NoteDocuments.relocate(from: old, to: new)
        registry.move(from: old, to: new)
        flashcards.moveNotes(from: old, to: new)
        library.reload()
    }
}
