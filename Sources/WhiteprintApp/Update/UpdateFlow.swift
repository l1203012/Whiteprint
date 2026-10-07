import AppKit

/// Walks the splash screen through checking for, downloading and installing
/// an update, telling the user what's happening and what they can do.
@MainActor
enum UpdateFlow {
    enum Trigger {
        /// At launch: install without asking, with Skip; say nothing when up to date.
        case launch
        /// From Check for Updates…: ask before installing, and report "up to date".
        case manual
    }

    /// Returns when the app should carry on. After a successful install it
    /// relaunches instead and doesn't return.
    static func run(_ updater: Updater, on splash: SplashWindow, trigger: Trigger) async {
        splash.update("Checking for updates…", progress: nil)
        let check = Task { try await updater.check() }
        splash.setButtons(["Skip"]) { _ in check.cancel() }
        let result = await check.result
        splash.setButtons([])
        // Skip cancels the request, which can fail with a URLError rather than CancellationError.
        if check.isCancelled { return }
        let update: AvailableUpdate?
        do {
            update = try result.get()
        } catch {
            NSLog("Whiteprint: update check failed: \(error)")
            if trigger == .manual {
                splash.update(error.localizedDescription, detail: "Check your connection and try again.", progress: 0)
                _ = await splash.choose(["OK"])
            } else {
                splash.update("No update check this time: GitHub couldn't be reached.", progress: 1)
                try? await Task.sleep(nanoseconds: 800_000_000)
            }
            return
        }

        guard let update else {
            if trigger == .manual {
                splash.update("You're up to date.", detail: "\(updater.current) is the newest version", progress: 1)
                _ = await splash.choose(["OK"])
            }
            return
        }
        let name = "Whiteprint \(update.version)"

        if let blocker = updater.blocker {
            splash.update(blocker.advice, detail: "\(update.version) available", progress: 1)
            if await splash.choose(["Download", "Continue"], timeout: trigger == .launch ? 12 : nil) == 0 {
                NSWorkspace.shared.open(update.page)
            }
            return
        }
        if trigger == .manual {
            splash.update("\(name) is available. Restart now to install it?", detail: "You're on \(updater.current)", progress: 0)
            guard await splash.choose(["Install and Restart", "Later"]) == 0 else { return }
        }

        let bytes = ByteCountFormatter()
        bytes.countStyle = .file
        // Progress from the download can arrive after it has finished.
        var downloaded = false
        let install = Task {
            try await updater.install(update) { step in
                switch step {
                case let .downloading(received, total):
                    guard !downloaded else { return }
                    splash.update("Downloading \(name)…",
                                  detail: "\(bytes.string(fromByteCount: received)) of \(bytes.string(fromByteCount: total))",
                                  progress: total > 0 ? Double(received) / Double(total) : nil)
                case .verifying:
                    downloaded = true
                    splash.update("Checking that the download is signed by Whiteprint…", progress: nil)
                case .installing:
                    splash.setButtons([])
                    splash.update("Installing \(name)… Please don't quit.", progress: nil)
                }
            }
        }
        splash.update("Downloading \(name)…", progress: 0)
        splash.setButtons(["Skip"]) { _ in install.cancel() }
        let installed = await install.result
        splash.setButtons([])
        if install.isCancelled {
            splash.update("Update skipped. Whiteprint will offer it again next time.", progress: 1)
            try? await Task.sleep(nanoseconds: 900_000_000)
            return
        }
        do {
            try installed.get()
        } catch {
            NSLog("Whiteprint: update to \(update.version) failed: \(error)")
            splash.update(error.localizedDescription, detail: "You can install \(update.version) by hand", progress: 1)
            if await splash.choose(["Download", "Continue"], timeout: trigger == .launch ? 12 : nil) == 0 {
                NSWorkspace.shared.open(update.page)
            }
            return
        }
        splash.update("Restarting into \(name)…", progress: 1)
        try? await Task.sleep(nanoseconds: 600_000_000)
        updater.relaunch(opening: NoteDocuments.open.compactMap(\.fileURL))
    }
}
