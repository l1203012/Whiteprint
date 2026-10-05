import Foundation
import WhiteprintBridge
import WhiteprintExtract
import WhiteprintStudy

extension Notification.Name {
    /// Posted on the main queue when imports, extraction or a Claude run change.
    static let studySessionDidChange = Notification.Name("WhiteprintStudySessionDidChange")
}

/// The study panel's state, kept app-wide so a run survives closing the panel:
/// imports in progress, the last errors, and the Claude Code run.
final class StudySession {
    enum RunState: Equatable {
        case idle, running, finished, failed(String)
    }

    let store: StudyStore?
    private(set) var imports: [StudyImport] = []
    /// File names being extracted right now.
    private(set) var extracting: [String] = []
    private(set) var importErrors: [String] = []
    private(set) var log: [String] = []
    private(set) var runState = RunState.idle
    /// nil until the first lookup finishes, then the result.
    private(set) var claudeLookup: URL??

    private var runner: ClaudeCodeRunner?
    private let queue = DispatchQueue(label: "io.github.l1203012.whiteprint.import", qos: .userInitiated)
    private static let logLimit = 200

    init(store: StudyStore?) {
        self.store = store
        imports = store?.imports ?? []
        NotificationCenter.default.addObserver(forName: .studyStoreDidChange, object: nil, queue: .main) { [weak self] _ in
            self?.refreshImports()
        }
    }

    var isRunning: Bool { runState == .running }

    /// Looks for `claude` off the main thread (it may ask the login shell).
    func locateClaude(force: Bool = false) {
        guard force || claudeLookup == nil else { return }
        DispatchQueue.global(qos: .userInitiated).async {
            let url = ClaudeCodeRunner.locateClaude()
            DispatchQueue.main.async {
                self.claudeLookup = .some(url)
                self.changed()
            }
        }
    }

    // MARK: Imports

    func importFiles(_ urls: [URL]) {
        guard let store else {
            importErrors = [WorkspaceError.studyUnavailable.description]
            return changed()
        }
        importErrors = []
        for url in urls {
            guard DocumentExtractor.supportedExtensions.contains(url.pathExtension.lowercased()) else {
                importErrors.append(WorkspaceError.unsupportedFile(url.lastPathComponent).description)
                continue
            }
            extracting.append(url.lastPathComponent)
            queue.async {
                let failure: String?
                do {
                    _ = try store.importFile(url)
                    failure = nil
                } catch {
                    failure = errorLine(error)
                }
                DispatchQueue.main.async {
                    if let index = self.extracting.firstIndex(of: url.lastPathComponent) { self.extracting.remove(at: index) }
                    if let failure { self.importErrors.append(failure) }
                    self.refreshImports()
                }
            }
        }
        changed()
    }

    func remove(_ importID: String) {
        do {
            try store?.remove(importID)
        } catch {
            importErrors = [errorLine(error)]
        }
        refreshImports()
    }

    func removeAll() throws {
        try store?.removeAll()
        refreshImports()
    }

    private func refreshImports() {
        imports = store?.imports ?? []
        changed()
    }

    // MARK: Claude Code

    /// Starts `claude -p` over every import. The study plan note opens by
    /// itself when Claude calls `build_study_plan`.
    func generate() {
        guard !isRunning, case .some(.some(let claude)) = claudeLookup else { return }
        let runner = ClaudeCodeRunner(claudeURL: claude)
        log = ["Starting Claude Code…"]
        runState = .running
        do {
            try runner.start(importIDs: imports.map(\.id), helperURL: BridgePaths.helperExecutable) { [weak self] event in
                self?.handle(event)
            }
            self.runner = runner
        } catch {
            runState = .failed(errorLine(error))
            log.append(errorLine(error))
        }
        changed()
    }

    func cancel() {
        guard isRunning else { return }
        runner?.cancel()
        runner = nil
        runState = .idle
        append("Cancelled.")
    }

    private func handle(_ event: ClaudeCodeRunner.Event) {
        switch event {
        case .status(let line):
            append(line)
        case .text(let text):
            let line = text.trimmingCharacters(in: .whitespacesAndNewlines)
            if !line.isEmpty { append(line) }
        case .finished:
            runner = nil
            runState = .finished
            append("Done. The study plan opened in a new window.")
        case .failed(let message):
            runner = nil
            runState = .failed(message)
            append(message)
        }
    }

    private func append(_ line: String) {
        log.append(line)
        if log.count > Self.logLimit { log.removeFirst(log.count - Self.logLimit) }
        changed()
    }

    private func changed() {
        NotificationCenter.default.post(name: .studySessionDidChange, object: self)
    }
}
