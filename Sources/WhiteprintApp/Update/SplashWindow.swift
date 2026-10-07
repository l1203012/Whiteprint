import AppKit

/// The launch screen: blueprint art, the logo, and a progress bar with a line
/// saying what Whiteprint is doing. Also shown by Check for Updates….
@MainActor
final class SplashWindow {
    static let disableEnvironmentKey = "WHITEPRINT_NO_SPLASH"
    static let size = NSSize(width: 680, height: 420)

    private static let tips = [
        "Tip: ⌘K opens the command palette, for notes, decks and actions.",
        "Tip: ⇧⌘D inserts a drawing. Help → Drawing Language has the syntax.",
        "Tip: ⇧⌘F inserts flashcards, and Note → Study Flashcards… reviews them.",
        "Tip: ⌘\\ shows or hides the sidebar.",
        "Tip: ⇧⌘M shows the Markdown behind your formatting.",
        "Tip: File → Import for Study Plan… turns course material into a plan.",
    ]

    let window: NSWindow
    private let status = SplashWindow.label(size: 13, weight: .medium, alpha: 0.95)
    private let detail = SplashWindow.label(size: 11, weight: .regular, alpha: 0.7)
    private let bar = SplashProgressBar()
    private let buttons = NSStackView()
    private let shownAt = Date()
    private var buttonContinuation: CheckedContinuation<Int, Never>?

    init(version: String) {
        window = NSWindow(contentRect: NSRect(origin: .zero, size: Self.size), styleMask: [.borderless], backing: .buffered, defer: false)
        window.isOpaque = false
        window.backgroundColor = .clear
        window.hasShadow = true
        window.level = .floating
        window.isReleasedWhenClosed = false
        window.isMovableByWindowBackground = true
        window.appearance = NSAppearance(named: .darkAqua)

        let art = BlueprintArtView(frame: NSRect(origin: .zero, size: Self.size))
        art.wantsLayer = true
        art.layer?.cornerRadius = 14
        art.layer?.masksToBounds = true
        window.contentView = art

        let logo = NSImageView()
        logo.image = Bundle.main.image(forResource: "SplashLogo") ?? NSApp.applicationIconImage
        logo.imageScaling = .scaleProportionallyUpOrDown
        let title = Self.label(size: 40, weight: .bold, alpha: 1)
        title.stringValue = "Whiteprint"
        let subtitle = Self.label(size: 14, weight: .regular, alpha: 0.75)
        subtitle.stringValue = "Notes, drawings and study plans"
        let versionLabel = Self.label(size: 11, weight: .regular, alpha: 0.6)
        versionLabel.stringValue = "Version \(version)"
        versionLabel.font = .monospacedDigitSystemFont(ofSize: 11, weight: .regular)
        let tip = Self.label(size: 11, weight: .regular, alpha: 0.6)
        tip.stringValue = Self.tips.randomElement() ?? ""
        tip.lineBreakMode = .byTruncatingTail
        let credit = Self.label(size: 10, weight: .regular, alpha: 0.45)
        credit.stringValue = "© 2026 Whiteprint · MIT License"
        detail.alignment = .right
        detail.font = .monospacedDigitSystemFont(ofSize: 11, weight: .regular)
        status.lineBreakMode = .byTruncatingTail
        buttons.orientation = .horizontal
        buttons.spacing = 8

        for view in [logo, title, subtitle, versionLabel, status, detail, bar, tip, credit, buttons] as [NSView] {
            view.translatesAutoresizingMaskIntoConstraints = false
            art.addSubview(view)
        }
        detail.setContentCompressionResistancePriority(.required, for: .horizontal)
        // Long messages truncate instead of widening the window.
        status.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        tip.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        NSLayoutConstraint.activate([
            logo.leadingAnchor.constraint(equalTo: art.leadingAnchor, constant: 40),
            logo.topAnchor.constraint(equalTo: art.topAnchor, constant: 56),
            logo.widthAnchor.constraint(equalToConstant: 104),
            logo.heightAnchor.constraint(equalToConstant: 104),
            title.leadingAnchor.constraint(equalTo: logo.trailingAnchor, constant: 14),
            title.bottomAnchor.constraint(equalTo: logo.centerYAnchor, constant: 8),
            subtitle.leadingAnchor.constraint(equalTo: title.leadingAnchor, constant: 2),
            subtitle.topAnchor.constraint(equalTo: title.bottomAnchor, constant: 2),
            versionLabel.leadingAnchor.constraint(equalTo: subtitle.leadingAnchor),
            versionLabel.topAnchor.constraint(equalTo: subtitle.bottomAnchor, constant: 6),

            credit.leadingAnchor.constraint(equalTo: art.leadingAnchor, constant: 40),
            credit.bottomAnchor.constraint(equalTo: art.bottomAnchor, constant: -18),
            tip.leadingAnchor.constraint(equalTo: credit.leadingAnchor),
            tip.trailingAnchor.constraint(lessThanOrEqualTo: art.trailingAnchor, constant: -40),
            tip.bottomAnchor.constraint(equalTo: credit.topAnchor, constant: -14),
            bar.leadingAnchor.constraint(equalTo: credit.leadingAnchor),
            bar.trailingAnchor.constraint(equalTo: art.trailingAnchor, constant: -40),
            bar.bottomAnchor.constraint(equalTo: tip.topAnchor, constant: -14),
            bar.heightAnchor.constraint(equalToConstant: 5),
            status.leadingAnchor.constraint(equalTo: bar.leadingAnchor),
            status.bottomAnchor.constraint(equalTo: bar.topAnchor, constant: -10),
            detail.trailingAnchor.constraint(equalTo: bar.trailingAnchor),
            detail.firstBaselineAnchor.constraint(equalTo: status.firstBaselineAnchor),
            status.trailingAnchor.constraint(lessThanOrEqualTo: detail.leadingAnchor, constant: -12),
            buttons.trailingAnchor.constraint(equalTo: bar.trailingAnchor),
            buttons.centerYAnchor.constraint(equalTo: credit.centerYAnchor, constant: -6),
        ])
        window.center()
    }

    func show() {
        window.alphaValue = 1
        window.makeKeyAndOrderFront(nil)
        window.displayIfNeeded()
    }

    /// Says what's happening; `progress` nil animates the bar instead.
    func update(_ text: String, detail: String = "", progress: Double?) {
        status.stringValue = text
        self.detail.stringValue = detail
        bar.fraction = progress
        window.displayIfNeeded()
    }

    /// Buttons under the bar; a click calls `action` with the button's index.
    func setButtons(_ titles: [String], action: ((Int) -> Void)? = nil) {
        buttons.arrangedSubviews.forEach { $0.removeFromSuperview() }
        for (index, title) in titles.enumerated() {
            let button = SplashButton(title: title) { [weak self] in
                action?(index)
                self?.buttonContinuation?.resume(returning: index)
                self?.buttonContinuation = nil
            }
            buttons.addArrangedSubview(button)
        }
    }

    /// Shows `titles` and waits for a click, or `timeout` seconds (then nil).
    func choose(_ titles: [String], timeout: TimeInterval? = nil) async -> Int? {
        setButtons(titles)
        let timer = timeout.map { seconds in
            Task { @MainActor [weak self] in
                try await Task.sleep(nanoseconds: UInt64(seconds * 1_000_000_000))
                self?.buttonContinuation?.resume(returning: -1)
                self?.buttonContinuation = nil
            }
        }
        let index = await withCheckedContinuation { self.buttonContinuation = $0 }
        timer?.cancel()
        setButtons([])
        return index >= 0 ? index : nil
    }

    /// Fades out, after the splash has been up at least `minimum` seconds so it doesn't just flash.
    func close(minimum: TimeInterval = 1.2) async {
        let remaining = minimum - Date().timeIntervalSince(shownAt)
        if remaining > 0 { try? await Task.sleep(nanoseconds: UInt64(remaining * 1_000_000_000)) }
        await NSAnimationContext.runAnimationGroup { context in
            context.duration = 0.25
            window.animator().alphaValue = 0
        }
        window.orderOut(nil)
    }

    private static func label(size: CGFloat, weight: NSFont.Weight, alpha: CGFloat) -> NSTextField {
        let label = NSTextField(labelWithString: "")
        label.font = .systemFont(ofSize: size, weight: weight)
        label.textColor = NSColor.white.withAlphaComponent(alpha)
        return label
    }
}

/// A thin white bar; nil `fraction` slides a segment back and forth.
private final class SplashProgressBar: NSView {
    private let fill = CALayer()

    var fraction: Double? {
        didSet { layoutFill() }
    }

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
        layer?.backgroundColor = NSColor.white.withAlphaComponent(0.18).cgColor
        layer?.cornerRadius = 2.5
        layer?.masksToBounds = true
        fill.backgroundColor = NSColor.white.cgColor
        fill.cornerRadius = 2.5
        layer?.addSublayer(fill)
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    override func layout() {
        super.layout()
        layoutFill()
    }

    private func layoutFill() {
        CATransaction.begin()
        CATransaction.setDisableActions(fraction == nil)
        fill.removeAnimation(forKey: "slide")
        if let fraction {
            fill.frame = NSRect(x: 0, y: 0, width: bounds.width * min(max(fraction, 0), 1), height: bounds.height)
        } else {
            let width = bounds.width * 0.28
            fill.frame = NSRect(x: 0, y: 0, width: width, height: bounds.height)
            let slide = CABasicAnimation(keyPath: "position.x")
            slide.fromValue = width / 2
            slide.toValue = bounds.width - width / 2
            slide.duration = 0.9
            slide.autoreverses = true
            slide.repeatCount = .infinity
            slide.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            fill.add(slide, forKey: "slide")
        }
        CATransaction.commit()
    }
}

/// A small outlined button that reads on the blueprint background.
private final class SplashButton: NSButton {
    private let handler: () -> Void

    init(title: String, handler: @escaping () -> Void) {
        self.handler = handler
        super.init(frame: .zero)
        isBordered = false
        wantsLayer = true
        layer?.cornerRadius = 6
        layer?.borderWidth = 1
        layer?.borderColor = NSColor.white.withAlphaComponent(0.55).cgColor
        layer?.backgroundColor = NSColor.white.withAlphaComponent(0.08).cgColor
        attributedTitle = NSAttributedString(string: title, attributes: [
            .foregroundColor: NSColor.white,
            .font: NSFont.systemFont(ofSize: 12, weight: .medium),
        ])
        target = self
        action = #selector(click)
        translatesAutoresizingMaskIntoConstraints = false
        heightAnchor.constraint(equalToConstant: 24).isActive = true
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    override var intrinsicContentSize: NSSize {
        NSSize(width: attributedTitle.size().width + 24, height: 24)
    }

    @objc private func click() { handler() }
}

/// Blueprint paper: a deep blue gradient, a drafting grid and a technical
/// drawing of a notebook and pen, with construction lines and dimensions.
private final class BlueprintArtView: NSView {
    override var isFlipped: Bool { true }
    override var mouseDownCanMoveWindow: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        NSGradient(colors: [
            NSColor(srgbRed: 0.10, green: 0.30, blue: 0.70, alpha: 1),
            NSColor(srgbRed: 0.05, green: 0.17, blue: 0.45, alpha: 1),
        ])?.draw(in: bounds, angle: 60)

        // The grid: fine lines every 12pt, stronger every 60pt.
        for (step, alpha) in [(CGFloat(12), 0.06), (60, 0.13)] {
            let grid = NSBezierPath()
            for x in stride(from: 0, through: bounds.width, by: step) {
                grid.move(to: NSPoint(x: x + 0.5, y: 0))
                grid.line(to: NSPoint(x: x + 0.5, y: bounds.height))
            }
            for y in stride(from: 0, through: bounds.height, by: step) {
                grid.move(to: NSPoint(x: 0, y: y + 0.5))
                grid.line(to: NSPoint(x: bounds.width, y: y + 0.5))
            }
            grid.lineWidth = 1
            NSColor.white.withAlphaComponent(alpha).setStroke()
            grid.stroke()
        }

        drawDrawing(origin: NSPoint(x: 410, y: 34))

        // A soft wash behind the text, so the drawing stays in the background.
        NSGradient(colors: [
            NSColor(srgbRed: 0.05, green: 0.15, blue: 0.40, alpha: 0),
            NSColor(srgbRed: 0.04, green: 0.13, blue: 0.36, alpha: 0.85),
        ])?.draw(in: NSRect(x: 0, y: bounds.height - 150, width: bounds.width, height: 150), angle: 90)
    }

    /// A notebook page in plan view with a pen across it, drawn like a technical sheet.
    private func drawDrawing(origin o: NSPoint) {
        let ink = NSColor.white.withAlphaComponent(0.55)
        let faint = NSColor.white.withAlphaComponent(0.28)
        func stroke(_ path: NSBezierPath, _ color: NSColor, width: CGFloat = 1, dash: [CGFloat] = []) {
            color.setStroke()
            path.lineWidth = width
            if !dash.isEmpty { path.setLineDash(dash, count: dash.count, phase: 0) }
            path.stroke()
        }

        // The page, its ruled lines and margin.
        let page = NSRect(x: o.x, y: o.y, width: 180, height: 230)
        stroke(NSBezierPath(roundedRect: page, xRadius: 10, yRadius: 10), ink, width: 1.5)
        let rules = NSBezierPath()
        for y in stride(from: page.minY + 36, to: page.maxY - 14, by: 18) {
            rules.move(to: NSPoint(x: page.minX + 26, y: y))
            rules.line(to: NSPoint(x: page.maxX - 12, y: y))
        }
        stroke(rules, faint)
        let margin = NSBezierPath()
        margin.move(to: NSPoint(x: page.minX + 20, y: page.minY))
        margin.line(to: NSPoint(x: page.minX + 20, y: page.maxY))
        stroke(margin, faint)

        // The pen, at 40°, with its centre line.
        let pen = NSBezierPath(roundedRect: NSRect(x: -10, y: -90, width: 20, height: 170), xRadius: 10, yRadius: 10)
        let nib = NSBezierPath()
        nib.move(to: NSPoint(x: -10, y: 70))
        nib.line(to: NSPoint(x: 0, y: 100))
        nib.line(to: NSPoint(x: 10, y: 70))
        let axis = NSBezierPath()
        axis.move(to: NSPoint(x: 0, y: -120))
        axis.line(to: NSPoint(x: 0, y: 130))
        var transform = AffineTransform(translationByX: page.maxX - 10, byY: page.minY + 120)
        transform.rotate(byDegrees: 40)
        for path in [pen, nib, axis] { path.transform(using: transform) }
        stroke(pen, ink, width: 1.5)
        stroke(nib, ink, width: 1.5)
        stroke(axis, faint, dash: [8, 3, 2, 3])

        // A compass arc showing the pen's angle.
        let pivot = NSPoint(x: page.maxX - 10, y: page.minY + 120)
        let arc = NSBezierPath()
        arc.appendArc(withCenter: pivot, radius: 60, startAngle: 90, endAngle: 130)
        stroke(arc, faint)
        Self.text("40°", at: NSPoint(x: pivot.x - 44, y: pivot.y + 62), alpha: 0.5)

        // Width and height dimensions.
        dimension(from: NSPoint(x: page.minX, y: page.maxY + 18), to: NSPoint(x: page.maxX, y: page.maxY + 18), label: "180")
        dimension(from: NSPoint(x: page.minX - 18, y: page.minY), to: NSPoint(x: page.minX - 18, y: page.maxY), label: "230")

        // A centre mark and a circle beside the page.
        let circle = NSBezierPath(ovalIn: NSRect(x: page.maxX + 22, y: page.minY + 150, width: 44, height: 44))
        stroke(circle, faint)
        let cross = NSBezierPath()
        let centre = NSPoint(x: circle.bounds.midX, y: circle.bounds.midY)
        cross.move(to: NSPoint(x: centre.x - 30, y: centre.y))
        cross.line(to: NSPoint(x: centre.x + 30, y: centre.y))
        cross.move(to: NSPoint(x: centre.x, y: centre.y - 30))
        cross.line(to: NSPoint(x: centre.x, y: centre.y + 30))
        stroke(cross, faint, dash: [6, 3])
        Self.text("Ø 44", at: NSPoint(x: centre.x - 12, y: centre.y + 32), alpha: 0.45)
        Self.text("SHEET 1 / 1   WHITEPRINT", at: NSPoint(x: page.minX + 20, y: page.minY - 16), alpha: 0.35)
    }

    private func dimension(from a: NSPoint, to b: NSPoint, label: String) {
        let color = NSColor.white.withAlphaComponent(0.4)
        let line = NSBezierPath()
        line.move(to: a)
        line.line(to: b)
        let angle = atan2(b.y - a.y, b.x - a.x)
        for (tip, direction) in [(a, angle), (b, angle + .pi)] {
            for spread in [-0.4, 0.4] {
                line.move(to: tip)
                line.line(to: NSPoint(x: tip.x + 7 * cos(direction + spread), y: tip.y + 7 * sin(direction + spread)))
            }
        }
        color.setStroke()
        line.lineWidth = 1
        line.stroke()
        Self.text(label, at: NSPoint(x: (a.x + b.x) / 2 - 10, y: (a.y + b.y) / 2 - 15), alpha: 0.5)
    }

    private static func text(_ string: String, at point: NSPoint, alpha: CGFloat) {
        (string as NSString).draw(at: point, withAttributes: [
            .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .medium),
            .foregroundColor: NSColor.white.withAlphaComponent(alpha),
        ])
    }
}
