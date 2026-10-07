import AppKit
import PDFKit
import WhiteprintCore
import WhiteprintEditor
import WhiteprintRender
import WhiteprintStudy

/// Developer screenshot tour for the README.
///
/// With `WHITEPRINT_SCREENSHOTS=<dir>` set, the app walks through a fixed set
/// of scenes on the notes in `WHITEPRINT_NOTES_DIR`, saves PNGs of its own
/// windows into `<dir>` and quits. An app may always capture its own windows,
/// so this needs no Screen Recording permission.
///
/// Optional: `WHITEPRINT_SCREENSHOTS_MATERIAL=<dir>` holds course files to
/// import for the study panel scene.
@MainActor
enum ScreenshotMode {
    static let environmentKey = "WHITEPRINT_SCREENSHOTS"
    static let materialKey = "WHITEPRINT_SCREENSHOTS_MATERIAL"

    /// Starts the tour when the environment asks for it. Call at launch,
    /// before any window opens.
    static func startIfRequested() {
        let environment = ProcessInfo.processInfo.environment
        guard let path = environment[environmentKey], !path.isEmpty else { return }
        let output = URL(fileURLWithPath: (path as NSString).expandingTildeInPath, isDirectory: true)
        let material = environment[materialKey].map { URL(fileURLWithPath: $0, isDirectory: true) }
        ScreenshotTour.prepare()
        Task { @MainActor in
            await ScreenshotTour(output: output, material: material).run()
        }
    }
}

@MainActor
private final class ScreenshotTour {
    static let windowSize = NSSize(width: 1280, height: 800)

    let output: URL
    let material: URL?

    init(output: URL, material: URL?) {
        self.output = output
        self.material = material
    }

    /// Fixed preferences, set before the first window reads them.
    static func prepare() {
        NSApp.appearance = NSAppearance(named: .aqua)
        AppDefaults.store.set(["Courses", "Courses/Networks"], forKey: "SidebarExpandedFolders")
        ViewPreferences.shared.layoutMode = .slides
        ViewPreferences.shared.showsMarkdownSyntax = false
    }

    func run() async {
        do {
            try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
            try await tour()
            log("done")
        } catch {
            log("failed: \(error)")
        }
        exit(0)
    }

    // MARK: Scenes

    private func tour() async throws {
        try renderStaticImages()

        NSApp.activate(ignoringOtherApps: true)
        guard let main = await waitFor({ NoteDocuments.frontWindow }) else { throw TourError("no note window opened") }
        let notes = AppServices.shared.library.folder.url
        let lecture = notes.appendingPathComponent("Courses/Networks/Lecture 3 – TCP.wprint")
        let plan = notes.appendingPathComponent("Courses/Networks/Study plan – Networks midterm.wprint")

        try await importMaterial()

        // Tabs: System design, then the lecture and the study plan.
        let lectureDocument = try NoteDocuments.show(lecture)
        let planDocument = try NoteDocuments.show(plan)
        let lectureWindow = try window(of: lectureDocument)
        let planWindow = try window(of: planDocument)
        for window in [main, lectureWindow, planWindow] { place(window) }
        main.makeKeyAndOrderFront(nil)
        main.makeFirstResponder(nil)
        await pause(1.5)

        // The editor: slides, syntax hidden; A4; syntax shown; dark chrome.
        capture([main], "main")
        ViewPreferences.shared.layoutMode = .a4
        await settle(main)
        capture([main], "a4-layout")
        ViewPreferences.shared.layoutMode = .slides
        ViewPreferences.shared.showsMarkdownSyntax = true
        await settle(main)
        capture([main], "markdown-syntax")
        ViewPreferences.shared.showsMarkdownSyntax = false
        NSApp.appearance = NSAppearance(named: .darkAqua)
        await settle(main)
        capture([main], "main-dark")
        NSApp.appearance = NSAppearance(named: .aqua)
        await settle(main)

        // Drawing popover: source and live preview.
        if let drawing = views(named: "DrawingBlockView", in: main.contentView).first {
            click(drawing, count: 2)
            await pause(1.2)
            if let popover = visibleWindow(classContaining: "Popover") {
                capture([popover, main], "drawing-editor")
                popover.close()
                await pause(0.6)
            }
            main.makeFirstResponder(nil)
        }

        // Flashcard deck on the lecture's second page, one answer revealed.
        lectureWindow.makeKeyAndOrderFront(nil)
        await pause(0.8)
        // Twice: realizing pages above can move the target page.
        for _ in 0..<2 {
            (lectureWindow.windowController as? NoteWindowController)?.scrollToPage(2)
            await pause(0.8)
        }
        if let deck = views(named: "DeckBlockView", in: lectureWindow.contentView).first {
            click(deck, at: NSPoint(x: deck.bounds.midX, y: 92))
            await pause(0.3)
            lectureWindow.makeFirstResponder(nil)
        }
        await settle(lectureWindow)
        capture([lectureWindow], "flashcard-deck")

        // Study window: question, then answer.
        FlashcardStudyWindowController.show(note: lecture, deckID: "c1")
        guard let study = await waitFor({ NSApp.windows.first { $0.title == "Study Flashcards" && $0.isVisible } }) else {
            throw TourError("the study window didn't open")
        }
        study.setContentSize(NSSize(width: 720, height: 560))
        study.center()
        await settle(study)
        capture([study], "flashcards-question")
        press(" ", keyCode: 49, in: study)
        await settle(study)
        capture([study], "flashcards-answer")
        study.close()

        // Command palette over the System design note.
        main.makeKeyAndOrderFront(nil)
        await pause(0.8)
        CommandPalette.shared.show(over: main)
        await pause(1)
        if let palette = visibleWindow(classContaining: "PalettePanel") {
            capture([palette, main], "command-palette")
            palette.orderOut(nil)
        }

        // Study panel, as a sheet.
        StudyPanelController.present(over: main)
        await pause(2.5)
        if let sheet = main.attachedSheet {
            capture([sheet, main], "study-panel")
            main.endSheet(sheet)
            await pause(0.6)
        }

        // The study plan note Claude wrote.
        planWindow.makeKeyAndOrderFront(nil)
        planWindow.makeFirstResponder(nil)
        await settle(planWindow)
        capture([planWindow], "study-plan")

        // Settings ▸ AI.
        (NSApp.delegate as? AppDelegate)?.showSettings(nil)
        guard let settings = await waitFor({ NSApp.windows.first { $0.windowController is SettingsWindowController && $0.isVisible } }) else {
            throw TourError("settings didn't open")
        }
        await pause(2.5)
        tidyPaths(in: settings.contentView)
        settings.makeFirstResponder(nil)
        await settle(settings)
        capture([settings], "settings-ai")
        settings.close()

        // Slash menu, typed into the System design note (last: it edits the note).
        main.makeKeyAndOrderFront(nil)
        await pause(0.8)
        let marker = "design review."
        if let text = views(named: "BlockTextView", in: main.contentView).compactMap({ $0 as? NSTextView })
            .first(where: { $0.string.contains(marker) }) {
            main.makeFirstResponder(text)
            let end = NSMaxRange((text.string as NSString).range(of: marker))
            text.setSelectedRange(NSRange(location: end, length: 0))
            text.insertText("\n", replacementRange: text.selectedRange())
            text.insertText("/", replacementRange: text.selectedRange())
            await settle(main)
            capture([main], "slash-menu")
        } else {
            log("no text block for the slash menu")
        }
    }

    // MARK: Material for the study panel

    private func importMaterial() async throws {
        guard let material else { return }
        let files = try FileManager.default.contentsOfDirectory(at: material, includingPropertiesForKeys: nil)
            .filter { !$0.lastPathComponent.hasPrefix(".") }
            .sorted { $0.lastPathComponent < $1.lastPathComponent }
        guard !files.isEmpty else { return }
        let session = AppServices.shared.study
        session.importFiles(files)
        _ = await waitFor(timeout: 60) { session.extracting.isEmpty && session.imports.count >= files.count ? true : nil }
        // Points Claude saved for the first chunk of the first file, so it shows progress.
        if let store = AppServices.shared.studyStore, let first = session.imports.first {
            try store.savePoints([
                StudyPoint(text: "TCP gives a reliable, ordered byte stream on top of IP", importance: .must, ref: "p. 1"),
                StudyPoint(text: "Three-way handshake: SYN, SYN-ACK, ACK", importance: .must, ref: "p. 1"),
            ], importID: first.id, chunk: 1)
            NotificationCenter.default.post(name: .studyStoreDidChange, object: nil)
        }
    }

    // MARK: Images that need no window

    private func renderStaticImages() throws {
        try renderDrawingExample()
        try renderPDFExport()
        try renderInstallWindow()
    }

    /// The drawing language next to what it draws.
    private func renderDrawingExample() throws {
        let source = """
        # Checkout service
        flow Web>API>Orders>Postgres
        db Postgres
        box Payments 28,10 "Payments\\n(Stripe)"
        box Queue 42,10 "Order events" dashed
        arrow Orders>Payments "charge"
        arrow Orders>Queue
        """
        let compiled = DrawingCompiler.compile(source)
        let canvas = SceneRenderer.canvasSize(for: compiled.scene)
        let size = CGSize(width: 1200, height: 420)
        let split: CGFloat = 470
        try writePNG("drawing-language", size: size) { context in
            // Source pane.
            let pane = CGRect(x: 0, y: 0, width: split, height: size.height)
            NSColor(srgbRed: 0.11, green: 0.13, blue: 0.17, alpha: 1).setFill()
            NSBezierPath(roundedRect: pane, xRadius: 14, yRadius: 14).fill()
            NSRect(x: split - 14, y: 0, width: 14, height: size.height).fill()
            let label = NSAttributedString(string: "Whiteprint drawing language", attributes: [
                .font: NSFont.systemFont(ofSize: 13, weight: .semibold),
                .foregroundColor: NSColor(white: 1, alpha: 0.55),
            ])
            label.draw(at: CGPoint(x: 32, y: 30))
            self.highlighted(source).draw(in: CGRect(x: 32, y: 74, width: split - 56, height: size.height - 100))

            // Drawing pane, on a blueprint sheet.
            let sheet = CGRect(x: split, y: 0, width: size.width - split, height: size.height)
            context.saveGState()
            let clip = NSBezierPath(roundedRect: sheet, xRadius: 14, yRadius: 14)
            clip.append(NSBezierPath(rect: NSRect(x: split, y: 0, width: 14, height: size.height)))
            clip.addClip()
            BlueprintBackground.draw(in: context, rect: sheet, palette: .blueprint)
            let scale = min(1.4, (sheet.width - 80) / canvas.width, (sheet.height - 60) / canvas.height)
            context.translateBy(x: sheet.midX - canvas.width * scale / 2, y: sheet.midY - canvas.height * scale / 2)
            context.scaleBy(x: scale, y: scale)
            SceneRenderer.draw(compiled.scene, in: context, palette: .blueprint)
            context.restoreGState()
        }
        if !compiled.errors.isEmpty { log("drawing example errors: \(compiled.errors)") }
    }

    /// Drawing-language source with commands, strings and comments coloured.
    private func highlighted(_ source: String) -> NSAttributedString {
        let font = NSFont.monospacedSystemFont(ofSize: 17, weight: .regular)
        let plain = NSColor(white: 0.92, alpha: 1)
        let paragraph = NSMutableParagraphStyle()
        paragraph.lineSpacing = 9
        let text = NSMutableAttributedString(string: source, attributes: [.font: font, .foregroundColor: plain, .paragraphStyle: paragraph])
        let ns = source as NSString
        func color(_ pattern: String, _ color: NSColor, bold: Bool = false) {
            guard let regex = try? NSRegularExpression(pattern: pattern, options: .anchorsMatchLines) else { return }
            for match in regex.matches(in: source, range: NSRange(location: 0, length: ns.length)) {
                text.addAttribute(.foregroundColor, value: color, range: match.range)
                if bold {
                    text.addAttribute(.font, value: NSFont.monospacedSystemFont(ofSize: 17, weight: .semibold), range: match.range)
                }
            }
        }
        color("^(flow|db|box|arrow|line|row|col|text|circle|group|dim|path)\\b", NSColor(srgbRed: 0.62, green: 0.83, blue: 1, alpha: 1), bold: true)
        color("\\b\\d+,\\d+\\b", NSColor(srgbRed: 0.98, green: 0.78, blue: 0.47, alpha: 1))
        color("\\b(dashed|thick|bold)\\b", NSColor(srgbRed: 0.98, green: 0.78, blue: 0.47, alpha: 1))
        color("\"[^\"]*\"", NSColor(srgbRed: 0.6, green: 0.88, blue: 0.62, alpha: 1))
        color("^#.*$", NSColor(white: 1, alpha: 0.42))
        return text
    }

    /// Page 1 of a note exported as PDF, Blueprint and Print side by side.
    private func renderPDFExport() throws {
        let url = AppServices.shared.library.folder.url.appendingPathComponent("Courses/Networks/Lecture 3 – TCP.wprint")
        let note = try NotesLibrary.read(url)
        var components = DateComponents()
        (components.year, components.month, components.day) = (2026, 10, 5)
        let date = Calendar(identifier: .gregorian).date(from: components) ?? Date()
        let pages = [PDFExportStyle.blueprint, .print].compactMap { style in
            PDFDocument(data: PDFExporter.data(for: note, style: style, date: date))?.page(at: 0)
        }
        guard pages.count == 2 else { throw TourError("PDF export produced no pages") }
        let box = pages[0].bounds(for: .mediaBox)
        let pageSize = CGSize(width: 480, height: (480 * box.height / box.width).rounded())
        let gap: CGFloat = 40, inset: CGFloat = 30
        let size = CGSize(width: inset * 2 + pageSize.width * 2 + gap, height: inset * 2 + pageSize.height)
        try writePNG("pdf-export", size: size) { context in
            for (index, page) in pages.enumerated() {
                let frame = CGRect(x: inset + CGFloat(index) * (pageSize.width + gap), y: inset, width: pageSize.width, height: pageSize.height)
                context.saveGState()
                context.setShadow(offset: CGSize(width: 0, height: 8), blur: 22, color: NSColor(white: 0, alpha: 0.28).cgColor)
                NSColor.white.setFill()
                frame.fill()
                context.restoreGState()
                context.saveGState()
                // PDF pages draw y-up.
                context.translateBy(x: frame.minX, y: frame.maxY)
                context.scaleBy(x: frame.width / box.width, y: -frame.height / box.height)
                page.draw(with: .mediaBox, to: context)
                context.restoreGState()
            }
        }
    }

    /// The DMG window as Finder lays it out (Scripts/release.sh): the
    /// background, the app at (160, 205) and Applications at (480, 205),
    /// 128 pt icons with 13 pt labels, in a dark-mode Finder window.
    private func renderInstallWindow() throws {
        guard let backgroundPath = ProcessInfo.processInfo.environment["WHITEPRINT_SCREENSHOTS_DMG_BACKGROUND"],
              let background = NSImage(contentsOfFile: backgroundPath) else { return }
        let content = CGSize(width: 640, height: 400)
        let titleBar: CGFloat = 28
        let margin: CGFloat = 40
        let size = CGSize(width: content.width + 2 * margin, height: content.height + titleBar + 2 * margin)
        let link = FileManager.default.temporaryDirectory.appendingPathComponent("wp-shots-Applications")
        try? FileManager.default.removeItem(at: link)
        try? FileManager.default.createSymbolicLink(at: link, withDestinationURL: URL(fileURLWithPath: "/Applications"))
        let applications = NSWorkspace.shared.icon(forFile: link.path)
        let app = NSApp.applicationIconImage ?? NSImage()
        try writePNG("install-dmg", size: size) { context in
            let window = CGRect(x: margin, y: margin * 0.7, width: content.width, height: content.height + titleBar)
            let shape = NSBezierPath(roundedRect: window, xRadius: 10, yRadius: 10)
            context.saveGState()
            context.setShadow(offset: CGSize(width: 0, height: 14), blur: 30, color: NSColor(white: 0, alpha: 0.45).cgColor)
            NSColor(white: 0.16, alpha: 1).setFill()
            shape.fill()
            context.restoreGState()
            context.saveGState()
            shape.addClip()
            // Title bar.
            NSColor(white: 0.21, alpha: 1).setFill()
            CGRect(x: window.minX, y: window.minY, width: window.width, height: titleBar).fill()
            for (i, color) in [NSColor(srgbRed: 1, green: 0.37, blue: 0.34, alpha: 1),
                               NSColor(srgbRed: 1, green: 0.74, blue: 0.18, alpha: 1),
                               NSColor(srgbRed: 0.16, green: 0.79, blue: 0.25, alpha: 1)].enumerated() {
                color.setFill()
                NSBezierPath(ovalIn: CGRect(x: window.minX + 12 + CGFloat(i) * 20, y: window.minY + 8, width: 12, height: 12)).fill()
            }
            let title = NSAttributedString(string: "Whiteprint", attributes: [
                .font: NSFont.systemFont(ofSize: 13, weight: .semibold), .foregroundColor: NSColor(white: 0.86, alpha: 1),
            ])
            title.draw(at: CGPoint(x: window.midX - title.size().width / 2, y: window.minY + 6))
            // Content.
            let origin = CGPoint(x: window.minX, y: window.minY + titleBar)
            background.draw(in: CGRect(origin: origin, size: content), from: .zero, operation: .sourceOver, fraction: 1,
                            respectFlipped: true, hints: nil)
            for (icon, name, center) in [(app, "Whiteprint", CGPoint(x: 160, y: 205)), (applications, "Applications", CGPoint(x: 480, y: 205))] {
                let frame = CGRect(x: origin.x + center.x - 64, y: origin.y + center.y - 64, width: 128, height: 128)
                icon.draw(in: frame, from: .zero, operation: .sourceOver, fraction: 1, respectFlipped: true, hints: nil)
                let label = NSAttributedString(string: name, attributes: [.font: NSFont.systemFont(ofSize: 13), .foregroundColor: NSColor.white])
                label.draw(at: CGPoint(x: frame.midX - label.size().width / 2, y: frame.maxY + 4))
            }
            context.restoreGState()
        }
        try? FileManager.default.removeItem(at: link)
    }

    // MARK: Windows

    private func window(of document: NoteDocument) throws -> NSWindow {
        guard let window = document.windowControllers.first?.window else { throw TourError("no window for \(document.displayName ?? "")") }
        return window
    }

    /// Same size and place every run, centred on the main screen.
    private func place(_ window: NSWindow) {
        let screen = (NSScreen.screens.first?.visibleFrame) ?? NSRect(x: 0, y: 0, width: 1440, height: 900)
        let size = Self.windowSize
        let origin = NSPoint(x: (screen.midX - size.width / 2).rounded(), y: (screen.midY - size.height / 2).rounded())
        window.setFrame(NSRect(origin: origin, size: size), display: true)
    }

    private func visibleWindow(classContaining name: String) -> NSWindow? {
        NSApp.windows.first { String(describing: type(of: $0)).contains(name) && $0.isVisible }
    }

    private func views(named name: String, in root: NSView?) -> [NSView] {
        guard let root else { return [] }
        var found: [NSView] = []
        if String(describing: type(of: root)) == name { found.append(root) }
        for view in root.subviews { found += views(named: name, in: view) }
        return found.filter { !$0.isHiddenOrHasHiddenAncestor && $0.visibleRect.height > 20 }
    }

    // MARK: Input

    private func click(_ view: NSView, count: Int = 1, at point: NSPoint? = nil) {
        guard let window = view.window else { return }
        let local = point ?? NSPoint(x: view.bounds.midX, y: view.bounds.midY)
        let location = view.convert(local, to: nil)
        for clickCount in 1...count {
            guard let event = NSEvent.mouseEvent(
                with: .leftMouseDown, location: location, modifierFlags: [], timestamp: ProcessInfo.processInfo.systemUptime,
                windowNumber: window.windowNumber, context: nil, eventNumber: 0, clickCount: clickCount, pressure: 1
            ) else { continue }
            view.mouseDown(with: event)
        }
    }

    private func press(_ characters: String, keyCode: UInt16, in window: NSWindow) {
        guard let event = NSEvent.keyEvent(
            with: .keyDown, location: .zero, modifierFlags: [], timestamp: ProcessInfo.processInfo.systemUptime,
            windowNumber: window.windowNumber, context: nil, characters: characters,
            charactersIgnoringModifiers: characters, isARepeat: false, keyCode: keyCode
        ) else { return }
        window.keyDown(with: event)
    }

    /// Settings shows where this copy of the app lives and where `claude` is;
    /// show what an installed copy shows instead, without the user's name.
    private func tidyPaths(in root: NSView?) {
        guard let root else { return }
        if let field = root as? NSTextField {
            var text = field.stringValue
            text = text.replacingOccurrences(of: Bundle.main.bundleURL.path, with: "/Applications/Whiteprint.app")
            if let claude = ClaudeCodeRunner.locateClaude()?.path {
                text = text.replacingOccurrences(of: "✓ Found at \(claude)", with: "✓ Found at ~/.local/bin/claude")
                text = text.replacingOccurrences(of: claude, with: "claude")
            }
            if text != field.stringValue { field.stringValue = text }
        }
        root.subviews.forEach(tidyPaths)
    }

    // MARK: Capture

    /// Saves `windows` (front first) as one PNG with their shadows, at the
    /// screen's resolution, trimmed to what was drawn.
    private func capture(_ windows: [NSWindow], _ name: String) {
        windows.forEach { $0.displayIfNeeded() }
        let image: CGImage?
        if windows.count == 1 {
            image = CGWindowListCreateImage(.null, .optionIncludingWindow, CGWindowID(windows[0].windowNumber), [.bestResolution])
        } else {
            let union = windows.dropFirst().reduce(windows[0].frame) { $0.union($1.frame) }.insetBy(dx: -70, dy: -70)
            let screenHeight = NSScreen.screens.first?.frame.height ?? 0
            let rect = CGRect(x: union.minX, y: screenHeight - union.maxY, width: union.width, height: union.height)
            var ids = windows.map { UnsafeRawPointer(bitPattern: UInt($0.windowNumber)) }
            let array = CFArrayCreate(kCFAllocatorDefault, &ids, ids.count, nil)
            image = array.flatMap { CGImage(windowListFromArrayScreenBounds: rect, windowArray: $0, imageOption: [.bestResolution]) }.flatMap(trimmed)
        }
        guard let image else { return log("couldn't capture \(name)") }
        save(NSBitmapImageRep(cgImage: image), name)
    }

    /// `image` cropped to its non-transparent pixels.
    private func trimmed(_ image: CGImage) -> CGImage? {
        let width = image.width, height = image.height
        guard let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
                                      space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue),
              let data = context.data?.assumingMemoryBound(to: UInt8.self) else { return image }
        context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
        var (minX, minY, maxX, maxY) = (width, height, -1, -1)
        for y in 0..<height {
            for x in 0..<width where data[(y * width + x) * 4 + 3] > 2 {
                minX = min(minX, x); maxX = max(maxX, x)
                minY = min(minY, y); maxY = max(maxY, y)
            }
        }
        guard maxX >= minX, maxY >= minY else { return image }
        return image.cropping(to: CGRect(x: minX, y: minY, width: maxX - minX + 1, height: maxY - minY + 1))
    }

    /// Draws into a y-down canvas of `size` points at 2× and saves it.
    private func writePNG(_ name: String, size: CGSize, draw: (CGContext) throws -> Void) throws {
        let scale: CGFloat = 2
        guard let rep = NSBitmapImageRep(
            bitmapDataPlanes: nil, pixelsWide: Int(size.width * scale), pixelsHigh: Int(size.height * scale),
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
            bytesPerRow: 0, bitsPerPixel: 0
        ), let graphics = NSGraphicsContext(bitmapImageRep: rep) else { throw TourError("no bitmap for \(name)") }
        rep.size = size
        let context = graphics.cgContext
        context.translateBy(x: 0, y: size.height * scale)
        context.scaleBy(x: scale, y: -scale)
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: context, flipped: true)
        defer { NSGraphicsContext.restoreGraphicsState() }
        try draw(context)
        context.flush()
        save(rep, name)
    }

    private func save(_ rep: NSBitmapImageRep, _ name: String) {
        let url = output.appendingPathComponent(name + ".png")
        do {
            guard let data = rep.representation(using: .png, properties: [:]) else { throw TourError("PNG encoding failed") }
            try data.write(to: url)
            log("saved \(url.lastPathComponent) (\(rep.pixelsWide)×\(rep.pixelsHigh))")
        } catch {
            log("couldn't save \(name): \(error)")
        }
    }

    // MARK: Timing

    private func pause(_ seconds: Double) async {
        try? await Task.sleep(nanoseconds: UInt64(seconds * 1_000_000_000))
    }

    /// Lets layout, animations and redraws finish.
    private func settle(_ window: NSWindow) async {
        await pause(0.9)
        window.layoutIfNeeded()
        window.displayIfNeeded()
        await pause(0.3)
    }

    private func waitFor<T>(timeout: Double = 15, _ value: () -> T?) async -> T? {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if let value = value() { return value }
            await pause(0.2)
        }
        return value()
    }

    private func log(_ message: String) {
        FileHandle.standardError.write(Data("screenshots: \(message)\n".utf8))
    }
}

private struct TourError: Error, CustomStringConvertible {
    let description: String

    init(_ description: String) {
        self.description = description
    }
}
