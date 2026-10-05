import AppKit

/// A small read-only text window, used for the drawing language reference.
enum ReferenceWindow {
    static func make(title: String, text: String) -> NSWindowController {
        let textView = NSTextView()
        textView.string = text
        textView.isEditable = false
        textView.font = .monospacedSystemFont(ofSize: 12, weight: .regular)
        textView.textContainerInset = NSSize(width: 16, height: 16)
        textView.isVerticallyResizable = true
        textView.autoresizingMask = .width
        let scroll = NSScrollView(frame: NSRect(x: 0, y: 0, width: 620, height: 560))
        scroll.documentView = textView
        scroll.hasVerticalScroller = true
        textView.frame = scroll.bounds

        let window = NSWindow(
            contentRect: scroll.frame, styleMask: [.titled, .closable, .resizable, .miniaturizable],
            backing: .buffered, defer: true
        )
        window.title = title
        window.contentView = scroll
        window.isReleasedWhenClosed = false
        window.center()
        return NSWindowController(window: window)
    }
}
