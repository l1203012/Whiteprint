import AppKit

/// Checks GitHub Releases for a newer Whiteprint, then downloads, verifies and
/// installs it over the running app and relaunches.
///
/// An update is trusted only when its DMG carries an Ed25519 signature from the
/// key whose public half is `WhiteprintUpdatePublicKey` in Info.plist (see
/// docs/RELEASING.md). Without that key, or in a build that isn't a released
/// app bundle, there is no updater.
@MainActor
final class Updater {
    static let launchCheckKey = "CheckForUpdatesOnLaunch"
    static let disableEnvironmentKey = "WHITEPRINT_NO_UPDATES"

    enum Step {
        case downloading(received: Int64, total: Int64)
        case verifying
        case installing
    }

    enum Failure: LocalizedError {
        case offline
        case badSignature
        case badPackage(String)
        case tool(String, String)

        var errorDescription: String? {
            switch self {
            case .offline: return "Couldn't reach GitHub to check for updates."
            case .badSignature: return "The download isn't signed by Whiteprint, so it wasn't installed."
            case .badPackage(let why): return "The download looks wrong (\(why)), so it wasn't installed."
            case .tool(let tool, let output): return "\(tool) failed: \(output)"
            }
        }
    }

    /// Why this copy can't replace itself, with what to do about it.
    enum Blocker {
        case notInApplications
        case readOnly

        var advice: String {
            switch self {
            case .notInApplications:
                return "Move Whiteprint to Applications to get updates automatically."
            case .readOnly:
                return "Whiteprint can't update its own folder. Download the update by hand."
            }
        }
    }

    let current: AppVersion
    let app: URL
    private let repository: String
    private let publicKey: String
    private let session: URLSession

    /// The updater for this copy, or nil when it can't update (a debug or edge
    /// build, no signing key, or turned off).
    static func make(bundle: Bundle = .main) -> Updater? {
        guard ProcessInfo.processInfo.environment[disableEnvironmentKey] == nil,
              bundle.bundleURL.pathExtension == "app",
              let text = bundle.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String,
              let version = AppVersion(text), !version.isEdge,
              let repository = bundle.object(forInfoDictionaryKey: "WhiteprintUpdateRepository") as? String, !repository.isEmpty,
              let key = bundle.object(forInfoDictionaryKey: "WhiteprintUpdatePublicKey") as? String, !key.isEmpty
        else { return nil }
        return Updater(current: version, app: bundle.bundleURL, repository: repository, publicKey: key)
    }

    static var checksOnLaunch: Bool {
        AppDefaults.store.object(forKey: launchCheckKey) as? Bool ?? true
    }

    init(current: AppVersion, app: URL, repository: String, publicKey: String) {
        self.current = current
        self.app = app
        self.repository = repository
        self.publicKey = publicKey
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 8
        session = URLSession(configuration: configuration)
    }

    /// Whether this copy can replace itself.
    var blocker: Blocker? {
        // A quarantined app opened from Downloads runs from a read-only random path.
        if app.path.contains("/AppTranslocation/") || app.path.hasPrefix("/Volumes/") { return .notInApplications }
        let fileManager = FileManager.default
        guard fileManager.isWritableFile(atPath: app.deletingLastPathComponent().path),
              fileManager.isWritableFile(atPath: app.path) else { return .readOnly }
        return nil
    }

    /// The newest update for this Mac, or nil when this copy is up to date.
    func check() async throws -> AvailableUpdate? {
        guard let url = UpdateFeed.releasesURL(repository: repository) else { return nil }
        var request = URLRequest(url: url)
        request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        let data: Data
        do {
            let (body, response) = try await session.data(for: request)
            guard (response as? HTTPURLResponse)?.statusCode == 200 else { throw Failure.offline }
            data = body
        } catch {
            try Task.checkCancellation()
            throw Failure.offline
        }
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        let releases = try decoder.decode([UpdateFeed.Release].self, from: data)
        return UpdateFeed.pick(from: releases, current: current, architecture: .current)
    }

    /// Downloads, verifies and installs `update` over this app. Cancelling is
    /// safe until `.installing` is reported.
    func install(_ update: AvailableUpdate, progress: @escaping (Step) -> Void) async throws {
        let fileManager = FileManager.default
        // On the app's volume, so the final swap is a rename.
        let work = try fileManager.url(for: .itemReplacementDirectory, in: .userDomainMask, appropriateFor: app, create: true)
        defer { try? fileManager.removeItem(at: work) }

        let (signature, _) = try await session.data(from: update.signature)
        progress(.downloading(received: 0, total: Int64(update.size)))
        let dmg = try await Download(url: update.dmg, to: work.appendingPathComponent("Whiteprint.dmg")) { received, total in
            progress(.downloading(received: received, total: total > 0 ? total : Int64(update.size)))
        }.run()

        progress(.verifying)
        let package = try Data(contentsOf: dmg, options: .alwaysMapped)
        guard UpdateFeed.isSigned(package, signature: String(decoding: signature, as: UTF8.self), publicKey: publicKey) else {
            throw Failure.badSignature
        }
        let mount = work.appendingPathComponent("mount")
        try fileManager.createDirectory(at: mount, withIntermediateDirectories: true)
        try await Self.run("/usr/bin/hdiutil", "attach", dmg.path, "-nobrowse", "-readonly", "-noautoopen", "-mountpoint", mount.path)
        let staged = work.appendingPathComponent("Whiteprint.app")
        do {
            try await Self.run("/usr/bin/ditto", mount.appendingPathComponent("Whiteprint.app").path, staged.path)
            try? await Self.run("/usr/bin/hdiutil", "detach", mount.path, "-quiet")
        } catch {
            try? await Self.run("/usr/bin/hdiutil", "detach", mount.path, "-force", "-quiet")
            throw error
        }
        try check(staged, is: update.version)
        try await Self.run("/usr/bin/codesign", "--verify", "--deep", "--strict", staged.path)
        try Task.checkCancellation()

        progress(.installing)
        _ = try fileManager.replaceItemAt(app, withItemAt: staged)
    }

    /// Opens the app again once this process has quit, with `documents`.
    func relaunch(opening documents: [URL]) {
        let script = "while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; app=$2; shift 2; exec /usr/bin/open -a \"$app\" \"$@\""
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/sh")
        process.arguments = ["-c", script, "sh", String(ProcessInfo.processInfo.processIdentifier), app.path] + documents.map(\.path)
        try? process.run()
        NSApp.terminate(nil)
    }

    private func check(_ staged: URL, is version: AppVersion) throws {
        guard let info = Bundle(url: staged)?.infoDictionary else { throw Failure.badPackage("no Whiteprint.app") }
        guard info["CFBundleIdentifier"] as? String == Bundle.main.bundleIdentifier else {
            throw Failure.badPackage("a different app")
        }
        guard (info["CFBundleShortVersionString"] as? String).flatMap(AppVersion.init) == version else {
            throw Failure.badPackage("not version \(version)")
        }
    }

    @discardableResult
    private static func run(_ tool: String, _ arguments: String...) async throws -> String {
        try await withCheckedThrowingContinuation { continuation in
            let process = Process()
            process.executableURL = URL(fileURLWithPath: tool)
            process.arguments = arguments
            let pipe = Pipe()
            process.standardOutput = pipe
            process.standardError = pipe
            process.terminationHandler = { process in
                let output = String(decoding: pipe.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
                if process.terminationStatus == 0 {
                    continuation.resume(returning: output)
                } else {
                    let name = (tool as NSString).lastPathComponent
                    continuation.resume(throwing: Failure.tool(name, output.trimmingCharacters(in: .whitespacesAndNewlines)))
                }
            }
            do {
                try process.run()
            } catch {
                continuation.resume(throwing: error)
            }
        }
    }
}

/// One file download with progress, cancelled with the task that awaits it.
private final class Download: NSObject, URLSessionDownloadDelegate {
    private let url: URL
    private let destination: URL
    private let progress: @MainActor (Int64, Int64) -> Void
    private var continuation: CheckedContinuation<URL, Error>?
    private var session: URLSession?

    init(url: URL, to destination: URL, progress: @escaping @MainActor (Int64, Int64) -> Void) {
        self.url = url
        self.destination = destination
        self.progress = progress
    }

    func run() async throws -> URL {
        let session = URLSession(configuration: .ephemeral, delegate: self, delegateQueue: nil)
        self.session = session
        defer { session.finishTasksAndInvalidate() }
        let task = session.downloadTask(with: url)
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                self.continuation = continuation
                task.resume()
            }
        } onCancel: {
            task.cancel()
        }
    }

    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didWriteData _: Int64,
                    totalBytesWritten: Int64, totalBytesExpectedToWrite: Int64) {
        let progress = self.progress
        Task { @MainActor in progress(totalBytesWritten, totalBytesExpectedToWrite) }
    }

    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {
        guard (downloadTask.response as? HTTPURLResponse)?.statusCode == 200 else {
            finish(.failure(Updater.Failure.offline))
            return
        }
        // The temporary file is deleted when this method returns.
        do {
            try FileManager.default.moveItem(at: location, to: destination)
            finish(.success(destination))
        } catch {
            finish(.failure(error))
        }
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        if let error {
            finish(.failure((error as? URLError)?.code == .cancelled ? CancellationError() : error))
        }
    }

    private func finish(_ result: Result<URL, Error>) {
        continuation?.resume(with: result)
        continuation = nil
    }
}
