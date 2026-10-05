import Foundation

/// Starts Whiteprint when `whiteprint-mcp` finds nothing listening on the socket.
public enum AppLauncher {
    /// Returns once the app accepts connections on `socketURL`. If it isn't
    /// running, opens the app bundle that contains this executable (falling
    /// back to the bundle id) and waits up to `timeout` for the socket.
    public static func ensureRunning(socketURL: URL = BridgePaths.socket, timeout: TimeInterval = 10) throws {
        let path = socketURL.path
        if UnixSocket.isAccepting(path) { return }

        let launched = containingBundle().map { open([$0.path]) } ?? false
        if !launched && !open(["-b", BridgePaths.appBundleID]) {
            throw BridgeError.launchFailed("Whiteprint.app not found (bundle id \(BridgePaths.appBundleID))")
        }

        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if UnixSocket.isAccepting(path) { return }
            usleep(100_000)
        }
        throw BridgeError.launchFailed("it didn't open its socket within \(Int(timeout)) s")
    }

    /// The `.app` this process runs from, with symlinks to the executable resolved.
    static func containingBundle() -> URL? {
        var size: UInt32 = 0
        _NSGetExecutablePath(nil, &size)
        var buffer = [CChar](repeating: 0, count: Int(size))
        guard _NSGetExecutablePath(&buffer, &size) == 0, let real = realpath(buffer, nil) else { return nil }
        defer { free(real) }
        return bundle(containing: URL(fileURLWithPath: String(cString: real)))
    }

    /// `X.app` for an executable at `X.app/Contents/MacOS/name`.
    static func bundle(containing executable: URL) -> URL? {
        let macOS = executable.deletingLastPathComponent()
        let contents = macOS.deletingLastPathComponent()
        let bundle = contents.deletingLastPathComponent()
        guard macOS.lastPathComponent == "MacOS", contents.lastPathComponent == "Contents",
              bundle.pathExtension == "app" else { return nil }
        return bundle
    }

    private static func open(_ arguments: [String]) -> Bool {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/open")
        process.arguments = arguments
        // stdout carries the MCP protocol; `open`'s complaints go to stderr, the log.
        process.standardOutput = FileHandle.standardError
        do {
            try process.run()
        } catch {
            return false
        }
        process.waitUntilExit()
        return process.terminationStatus == 0
    }
}
