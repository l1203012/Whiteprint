import Foundation
import WhiteprintBridge
import WhiteprintCore
import WhiteprintExtract
import WhiteprintStudy

extension Notification.Name {
    /// Posted on the main queue after imports or saved points change.
    static let studyStoreDidChange = Notification.Name("WhiteprintStudyStoreDidChange")
}

/// Answers the MCP helper's requests: note requests become Core edits applied
/// through the workspace, study requests go to the shared study store.
/// Replies are short text meant for Claude.
final class BridgeService: BridgeHandler {
    private let workspace: NoteWorkspace
    private let registry: NoteRegistry
    private let study: StudyStore?
    private let studyQueue = DispatchQueue(label: "io.github.l1203012.whiteprint.study", qos: .userInitiated, attributes: .concurrent)

    init(workspace: NoteWorkspace, registry: NoteRegistry, study: StudyStore?) {
        self.workspace = workspace
        self.registry = registry
        self.study = study
    }

    func handle(_ request: BridgeRequest, reply: @escaping (BridgeResponse) -> Void) {
        switch request {
        case .importDocument, .listImports, .readChunk, .savePoints, .getPoints:
            handleStudy(request, reply: reply)
        case .buildStudyPlan(let importIDs, let plan):
            buildStudyPlan(importIDs: importIDs, plan: plan, reply: reply)
        default:
            reply(Self.respond { try handleNote(request) })
        }
    }

    // MARK: Notes

    private func handleNote(_ request: BridgeRequest) throws -> String {
        switch request {
        case .ping:
            return "pong"

        case .listNotes:
            let lines = workspace.noteURLs().map { url -> String in
                let id = registry.id(for: url)
                guard let note = try? workspace.note(at: url) else {
                    return "\(id) \"\(NoteTitle.baseName(url))\" · unreadable"
                }
                let pages = note.pages.count
                return "\(id) \"\(NoteTitle.display(for: note, fileURL: url))\" · \(pages) page\(pages == 1 ? "" : "s")"
            }
            return lines.isEmpty ? "no notes" : lines.joined(separator: "\n")

        case let .readNote(id, page):
            let note = try workspace.note(at: url(for: id))
            if let page {
                let source = try note.pageSource(page)
                return source.isEmpty ? "(empty page)" : source
            }
            return try (1...note.pages.count)
                .map { "=== page \($0) ===\n\(try note.pageSource($0))" }
                .joined(separator: "\n\n")

        case let .createNote(title, markdown):
            var note = Note(title: title)
            if let markdown, !markdown.isEmpty {
                try note.write(markdown, page: 1, mode: .replace)
            }
            return "ok \(registry.id(for: try workspace.createNote(note, title: title)))"

        case let .write(id, page, markdown, mode):
            try workspace.edit(noteAt: url(for: id), actionName: "Claude’s Edit") { note in
                try note.write(markdown, page: page, mode: mode)
            }
            return "ok"

        case let .draw(id, page, dsl, after):
            let drawingID = try workspace.edit(noteAt: url(for: id), actionName: "Claude’s Drawing") { note in
                try note.insertDrawing(dsl, page: page, after: after)
            }
            return Self.withCompileErrors("ok \(drawingID)", dsl)

        case let .editDrawing(id, drawing, dsl):
            try workspace.edit(noteAt: url(for: id), actionName: "Claude’s Drawing") { note in
                try note.updateDrawing(drawing, source: dsl)
            }
            return Self.withCompileErrors("ok", dsl)

        case let .deleteDrawing(id, drawing):
            try workspace.edit(noteAt: url(for: id), actionName: "Delete Drawing") { note in
                try note.deleteDrawing(drawing)
            }
            return "ok"

        case let .addPage(id):
            let page = try workspace.edit(noteAt: url(for: id), actionName: "Add Page") { note in
                try note.addPage()
            }
            return "ok \(page)"

        default:
            preconditionFailure("not a note request: \(request)")
        }
    }

    private func url(for id: String) throws -> URL {
        guard let url = registry.url(for: id) else { throw WorkspaceError.unknownNote(id) }
        return url
    }

    /// `ok d3` followed by one `line N: …` per compile error.
    private static func withCompileErrors(_ head: String, _ dsl: String) -> String {
        ([head] + DrawingCompiler.compile(dsl).errors.map(\.description)).joined(separator: "\n")
    }

    // MARK: Study

    private func handleStudy(_ request: BridgeRequest, reply: @escaping (BridgeResponse) -> Void) {
        guard let study else { return reply(.failure(WorkspaceError.studyUnavailable.description)) }
        if case .importDocument(let path) = request {
            return importDocument(path, into: study, reply: reply)
        }
        studyQueue.async {
            let response = Self.respond { () -> String in
                switch request {
                case .listImports:
                    let lines = study.imports.map { item in
                        "\(item.id) \(item.name) · \(Self.summary(of: item, includingID: false)) · \(item.chunksDone)/\(item.chunkCount) done"
                    }
                    return lines.isEmpty ? "no imports" : lines.joined(separator: "\n")
                case let .readChunk(id, n):
                    return try study.chunk(id, n)
                case let .savePoints(id, n, points):
                    try study.savePoints(points, importID: id, chunk: n)
                    Self.postStudyChange()
                    return "ok"
                case .getPoints(let id):
                    let summary = try study.pointsSummary(importID: id)
                    return summary.isEmpty ? "no points saved" : summary
                default:
                    preconditionFailure("not a study request: \(request)")
                }
            }
            reply(response)
        }
    }

    private func importDocument(_ path: String, into study: StudyStore, reply: @escaping (BridgeResponse) -> Void) {
        let file: URL
        do { file = try Self.importableFile(path) } catch { return reply(.failure(errorLine(error))) }
        studyQueue.async {
            reply(Self.respond {
                let item = try study.importFile(file)
                Self.postStudyChange()
                return "ok " + Self.summary(of: item)
            })
        }
    }

    private func buildStudyPlan(importIDs: [String], plan: StudyPlan, reply: @escaping (BridgeResponse) -> Void) {
        guard let study else { return reply(.failure(WorkspaceError.studyUnavailable.description)) }
        let known = Set(study.imports.map(\.id))
        if let unknown = importIDs.first(where: { !known.contains($0) }) {
            return reply(.failure(StudyError.unknownImport(unknown).description))
        }
        reply(Self.respond {
            let note = StudyPlanRenderer.note(for: plan)
            let url = try workspace.createNote(note, title: "Study plan – \(plan.title)")
            return "ok \(registry.id(for: url))"
        })
    }

    /// Resolves `path` (absolute, or with `~`) to an existing file Whiteprint can extract.
    static func importableFile(_ path: String) throws -> URL {
        let url = URL(fileURLWithPath: (path.trimmingCharacters(in: .whitespacesAndNewlines) as NSString).expandingTildeInPath)
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: url.path, isDirectory: &isDirectory), !isDirectory.boolValue else {
            throw WorkspaceError.fileNotFound(path)
        }
        guard DocumentExtractor.supportedExtensions.contains(url.pathExtension.lowercased()) else {
            throw WorkspaceError.unsupportedFile(url.lastPathComponent)
        }
        return url
    }

    /// `i2 · 14 slides · 3 chunks`.
    static func summary(of item: StudyImport, includingID: Bool = true) -> String {
        let unit: String
        switch (item.name as NSString).pathExtension.lowercased() {
        case "pptx": unit = "slide"
        case "pdf": unit = "page"
        default: unit = "section"
        }
        let parts = [
            "\(item.unitCount) \(unit)\(item.unitCount == 1 ? "" : "s")",
            "\(item.chunkCount) chunk\(item.chunkCount == 1 ? "" : "s")",
        ]
        return ((includingID ? [item.id] : []) + parts).joined(separator: " · ")
    }

    static func postStudyChange() {
        DispatchQueue.main.async { NotificationCenter.default.post(name: .studyStoreDidChange, object: nil) }
    }

    private static func respond(_ body: () throws -> String) -> BridgeResponse {
        do { return .ok(try body()) } catch { return .failure(errorLine(error)) }
    }
}
