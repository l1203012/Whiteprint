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

    private init() {
        library = NotesLibrary()
        studyStore = try? StudyStore()
        study = StudySession(store: studyStore)
        bridge = BridgeService(workspace: DocumentWorkspace(library: library), registry: registry, study: studyStore)
        NotificationCenter.default.addObserver(forName: .noteDocumentDidMove, object: nil, queue: .main) { [registry] notification in
            guard let old = notification.userInfo?["old"] as? URL,
                  let new = (notification.object as? NoteDocument)?.fileURL else { return }
            registry.move(from: old, to: new)
        }
    }
}
