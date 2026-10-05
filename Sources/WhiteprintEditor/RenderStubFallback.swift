import AppKit
import WhiteprintCore
import WhiteprintRender

// TEMPORARY — delete this file once WhiteprintRender is merged.
//
// While WhiteprintRender is still the contract stub, `MarkdownStyler` does
// nothing and `DrawingView` draws nothing, so the editor can't be checked by
// eye. These stand-ins switch themselves on only when they detect the stub
// (a styled heading comes back without a font) and stay off with the real
// renderer. The editor always calls MarkdownStyler / DrawingView /
// SceneRenderer first; nothing here replaces them.
enum RenderStubFallback {
    static let isActive: Bool = {
        let probe = MarkdownStyler.attributedString(markdown: "# a", palette: .blueprint)
        return probe.length == 0 || probe.attribute(.font, at: 0, effectiveRange: nil) == nil
    }()

    /// Minimal heading / markup / code styling, whole storage.
    static func style(_ storage: NSTextStorage, palette: BlueprintPalette) {
        let ns = storage.string as NSString
        storage.beginEditing()
        storage.setAttributes(TextStyle.base(palette), range: NSRange(location: 0, length: ns.length))
        var fence: CodeFence.Opener?
        for line in TextLine.all(in: ns) where line.range.length > 0 || fence != nil {
            if let open = fence {
                if CodeFence.closes(open, line.text) { fence = nil }
                storage.addAttributes([.font: NSFont.monospacedSystemFont(ofSize: 13.5, weight: .regular)], range: line.range)
                if fence == nil { storage.addAttribute(.foregroundColor, value: palette.muted, range: line.range) }
                continue
            }
            if let open = CodeFence.opener(line.text) {
                fence = open
                storage.addAttributes([.font: NSFont.monospacedSystemFont(ofSize: 13.5, weight: .regular),
                                       .foregroundColor: palette.muted], range: line.range)
                continue
            }
            let parsed = MarkdownLine(line.text)
            let marker = NSRange(location: line.start, length: min(parsed.prefixLength, line.range.length))
            switch parsed.kind {
            case .heading(let level):
                let size: CGFloat = [28, 22, 18, 16, 15, 15][min(level, 6) - 1]
                let paragraph = NSMutableParagraphStyle()
                paragraph.paragraphSpacingBefore = level == 1 ? 6 : 4
                paragraph.paragraphSpacing = 6
                storage.addAttributes([.font: NSFont.systemFont(ofSize: size, weight: .bold), .paragraphStyle: paragraph],
                                      range: line.range)
            case .checklist(let checked, _):
                if checked {
                    let rest = NSRange(location: NSMaxRange(marker), length: line.end - NSMaxRange(marker))
                    storage.addAttributes([.foregroundColor: palette.muted, .strikethroughStyle: 1], range: rest)
                }
                if let box = parsed.checkboxRange {
                    storage.addAttribute(.foregroundColor, value: palette.accent,
                                         range: NSRange(location: line.start + box.location, length: box.length))
                }
            case .quote:
                storage.addAttribute(.foregroundColor, value: palette.muted, range: line.range)
            default:
                if line.text.trimmingCharacters(in: .whitespaces) == "---" {
                    storage.addAttribute(.foregroundColor, value: palette.muted, range: line.range)
                }
            }
            if parsed.prefixLength > parsed.indent.count, !isCheckbox(parsed) {
                storage.addAttribute(.foregroundColor, value: palette.muted, range: marker)
            }
        }
        storage.endEditing()
    }

    private static func isCheckbox(_ line: MarkdownLine) -> Bool {
        if case .checklist = line.kind { return true }
        return false
    }

    /// `SceneRenderer.canvasSize`, computed from the scene bounds.
    static func canvasSize(for scene: DrawingScene) -> CGSize {
        let bounds = scene.bounds
        return CGSize(width: bounds.maxX * SceneRenderer.unit + 2 * SceneRenderer.margin,
                      height: bounds.maxY * SceneRenderer.unit + 2 * SceneRenderer.margin)
    }

    /// Outlines of shapes, groups and lines, in canvas coordinates (y-down).
    static func draw(_ scene: DrawingScene, palette: BlueprintPalette) {
        let unit = SceneRenderer.unit, margin = SceneRenderer.margin
        func rect(_ r: GridRect) -> NSRect {
            NSRect(x: margin + r.minX * unit, y: margin + r.minY * unit, width: r.size.width * unit, height: r.size.height * unit)
        }
        func point(_ p: GridPoint) -> NSPoint { NSPoint(x: margin + p.x * unit, y: margin + p.y * unit) }
        palette.text.setStroke()
        let label: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: 12), .foregroundColor: palette.text]
        for group in scene.groups {
            let path = NSBezierPath(roundedRect: rect(group.frame), xRadius: 4, yRadius: 4)
            path.setLineDash([4, 3], count: 2, phase: 0)
            path.stroke()
        }
        for shape in scene.shapes {
            let r = rect(shape.frame)
            switch shape.kind {
            case .box: NSBezierPath(roundedRect: r, xRadius: 3, yRadius: 3).stroke()
            case .circle: NSBezierPath(ovalIn: r).stroke()
            case .db:
                NSBezierPath(roundedRect: r, xRadius: r.width / 2, yRadius: 8).stroke()
                NSBezierPath(ovalIn: NSRect(x: r.minX, y: r.minY, width: r.width, height: 16)).stroke()
            case .text: break
            }
            if let text = shape.label as NSString? {
                let size = text.size(withAttributes: label)
                text.draw(at: NSPoint(x: r.midX - size.width / 2, y: r.midY - size.height / 2), withAttributes: label)
            }
        }
        for line in scene.lines where line.points.count > 1 {
            let path = NSBezierPath()
            path.move(to: point(line.points[0]))
            for p in line.points.dropFirst() { path.line(to: point(p)) }
            path.stroke()
            if line.endArrow, let a = line.points.dropLast().last, let b = line.points.last {
                let tip = point(b), from = point(a)
                let angle = atan2(tip.y - from.y, tip.x - from.x)
                let head = NSBezierPath()
                head.move(to: NSPoint(x: tip.x - 8 * cos(angle - 0.4), y: tip.y - 8 * sin(angle - 0.4)))
                head.line(to: tip)
                head.line(to: NSPoint(x: tip.x - 8 * cos(angle + 0.4), y: tip.y - 8 * sin(angle + 0.4)))
                head.stroke()
            }
        }
    }
}
