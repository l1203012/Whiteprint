import AppKit

// The entry point is alone in this file so a test build can leave it out.
@main
enum WhiteprintMain {
    static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        app.setActivationPolicy(.regular)
        withExtendedLifetime(delegate) { app.run() }
    }
}
