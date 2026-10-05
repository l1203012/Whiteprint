import AppKit

// CONTRACT (owner: app agent): placeholder entry point.
@main
enum WhiteprintMain {
    static func main() {
        let app = NSApplication.shared
        app.setActivationPolicy(.regular)
        app.run()
    }
}
