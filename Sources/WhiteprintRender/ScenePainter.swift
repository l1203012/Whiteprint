import AppKit
import WhiteprintCore

/// Strokes and labels one scene into a y-down context whose origin is the
/// scene's grid origin. Expects a matching current `NSGraphicsContext` for text.
struct ScenePainter {
    static let lineWidth: CGFloat = 1.5
    static let thickLineWidth: CGFloat = 2.5
    static let fineLineWidth: CGFloat = 1
    static let dashPattern: [CGFloat] = [5, 3.5]
    static let cornerRadius: CGFloat = 4
    static let groupCornerRadius: CGFloat = 7
    static let labelFontSize: CGFloat = 12
    static let smallFontSize: CGFloat = 11

    let context: CGContext
    let palette: BlueprintPalette

    func draw(_ scene: DrawingScene) {
        scene.groups.forEach(drawGroup)
        scene.lines.forEach(drawLine)
        scene.shapes.forEach(drawShape)
        scene.dimensions.forEach(drawDimension)
    }

    // MARK: Shapes

    private func drawShape(_ shape: SceneShape) {
        let rect = shape.frame.canvasRect
        let bold = shape.style.contains(.bold)
        switch shape.kind {
        case .box:
            let radius = min(Self.cornerRadius, rect.width / 2, rect.height / 2)
            stroke(CGPath(roundedRect: rect, cornerWidth: radius, cornerHeight: radius, transform: nil), style: shape.style)
            drawLabel(shape.label, centeredIn: rect, bold: bold)
        case .circle:
            stroke(CGPath(ellipseIn: rect, transform: nil), style: shape.style)
            drawLabel(shape.label, centeredIn: rect, bold: bold)
        case .db:
            let cap = Self.cylinderCapHeight(for: rect)
            stroke(Self.cylinderPath(in: rect, capHeight: cap), style: shape.style)
            let body = CGRect(x: rect.minX, y: rect.minY + 2 * cap, width: rect.width, height: rect.height - 2 * cap)
            drawLabel(shape.label, centeredIn: body, bold: bold)
        case .text:
            drawText(shape.label, in: rect, bold: bold)
        }
    }

    /// Height of half the cylinder's top ellipse.
    static func cylinderCapHeight(for rect: CGRect) -> CGFloat {
        max(2, min(rect.height * 0.15, rect.width * 0.2, 8))
    }

    /// The whole top ellipse, the sides, and the front half of the bottom ellipse.
    static func cylinderPath(in rect: CGRect, capHeight cap: CGFloat) -> CGPath {
        let path = CGMutablePath()
        path.addEllipse(in: CGRect(x: rect.minX, y: rect.minY, width: rect.width, height: 2 * cap))
        let bottomY = rect.maxY - cap
        let k: CGFloat = 0.5523
        let rx = rect.width / 2
        path.move(to: CGPoint(x: rect.minX, y: rect.minY + cap))
        path.addLine(to: CGPoint(x: rect.minX, y: bottomY))
        path.addCurve(
            to: CGPoint(x: rect.midX, y: rect.maxY),
            control1: CGPoint(x: rect.minX, y: bottomY + k * cap),
            control2: CGPoint(x: rect.midX - k * rx, y: rect.maxY)
        )
        path.addCurve(
            to: CGPoint(x: rect.maxX, y: bottomY),
            control1: CGPoint(x: rect.midX + k * rx, y: rect.maxY),
            control2: CGPoint(x: rect.maxX, y: bottomY + k * cap)
        )
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY + cap))
        return path
    }

    // MARK: Lines

    private func drawLine(_ line: SceneLine) {
        let points = line.points.map(\.canvasPoint)
        guard points.count >= 2 else { return }
        let arrow = ArrowHead(thick: line.style.contains(.thick))
        var path = points
        if line.startArrow {
            path[0] = arrow.base(tip: points[0], from: points[1])
        }
        if line.endArrow {
            path[path.count - 1] = arrow.base(tip: points[points.count - 1], from: points[points.count - 2])
        }
        let cgPath = CGMutablePath()
        cgPath.addLines(between: path)
        if line.closed {
            cgPath.closeSubpath()
        }
        stroke(cgPath, style: line.style)
        context.setFillColor(palette.text.cgColor)
        if line.startArrow {
            context.addPath(arrow.path(tip: points[0], from: points[1]))
        }
        if line.endArrow {
            context.addPath(arrow.path(tip: points[points.count - 1], from: points[points.count - 2]))
        }
        context.fillPath()
        if let label = line.label, !label.isEmpty {
            drawKnockoutLabel(label, at: Self.midpoint(of: points))
        }
    }

    /// The point halfway along the polyline.
    static func midpoint(of points: [CGPoint]) -> CGPoint {
        let segments = zip(points, points.dropFirst()).map { ($0, $1, $0.distance(to: $1)) }
        var remaining = segments.reduce(0) { $0 + $1.2 } / 2
        for (a, b, length) in segments {
            if remaining <= length, length > 0 {
                return a.interpolated(to: b, fraction: remaining / length)
            }
            remaining -= length
        }
        return points[0]
    }

    // MARK: Dimensions

    private func drawDimension(_ dimension: SceneDimension) {
        let from = dimension.from.canvasPoint, to = dimension.to.canvasPoint
        let offset = dimension.offsetLine
        let start = offset.from.canvasPoint, end = offset.to.canvasPoint
        let path = CGMutablePath()
        path.move(to: start)
        path.addLine(to: end)
        if let normal = from.direction(to: start) {
            let gap: CGFloat = 3, overshoot: CGFloat = 4
            for (measured, mark) in [(from, start), (to, end)] {
                path.move(to: measured.offset(by: normal, distance: gap))
                path.addLine(to: mark.offset(by: normal, distance: overshoot))
            }
        }
        strokePlain(path, width: Self.fineLineWidth)
        if let along = start.direction(to: end) {
            let slash = CGPoint(x: along.x - along.y, y: along.y + along.x)
            let ticks = CGMutablePath()
            for mark in [start, end] {
                ticks.move(to: mark.offset(by: slash, distance: -3.5))
                ticks.addLine(to: mark.offset(by: slash, distance: 3.5))
            }
            strokePlain(ticks, width: Self.lineWidth)
        }
        drawKnockoutLabel(dimension.label, at: start.interpolated(to: end, fraction: 0.5))
    }

    // MARK: Groups

    private func drawGroup(_ group: SceneGroup) {
        let rect = group.frame.canvasRect
        let radius = min(Self.groupCornerRadius, rect.width / 2, rect.height / 2)
        let path = CGPath(roundedRect: rect, cornerWidth: radius, cornerHeight: radius, transform: nil)
        stroke(path, style: group.style, color: palette.muted, width: group.style.contains(.thick) ? Self.lineWidth * 1.4 : Self.fineLineWidth * 1.2)
        guard let label = group.label, !label.isEmpty else { return }
        let text = NSAttributedString(string: label, attributes: [
            .font: NSFont.systemFont(ofSize: Self.smallFontSize, weight: .medium),
            .foregroundColor: palette.muted,
        ])
        text.draw(at: CGPoint(x: rect.minX + 8, y: rect.minY + 4))
    }

    // MARK: Stroking

    private func stroke(_ path: CGPath, style: DrawingStyle, color: NSColor? = nil, width: CGFloat? = nil) {
        context.saveGState()
        context.setStrokeColor((color ?? palette.text).cgColor)
        context.setLineWidth(width ?? (style.contains(.thick) ? Self.thickLineWidth : Self.lineWidth))
        context.setLineJoin(.round)
        if style.contains(.dashed) {
            context.setLineCap(.butt)
            context.setLineDash(phase: 0, lengths: Self.dashPattern)
        } else {
            context.setLineCap(.round)
        }
        context.addPath(path)
        context.strokePath()
        context.restoreGState()
    }

    private func strokePlain(_ path: CGPath, width: CGFloat) {
        stroke(path, style: [], width: width)
    }

    // MARK: Text

    private func labelAttributes(bold: Bool, alignment: NSTextAlignment, size: CGFloat = labelFontSize) -> [NSAttributedString.Key: Any] {
        let paragraph = NSMutableParagraphStyle()
        paragraph.alignment = alignment
        paragraph.lineBreakMode = .byWordWrapping
        return [
            .font: NSFont.systemFont(ofSize: size, weight: bold ? .semibold : .regular),
            .foregroundColor: palette.text,
            .paragraphStyle: paragraph,
        ]
    }

    private func drawLabel(_ label: String?, centeredIn rect: CGRect, bold: Bool) {
        guard let label, !label.isEmpty else { return }
        let text = NSAttributedString(string: label, attributes: labelAttributes(bold: bold, alignment: .center))
        let width = max(rect.width - 8, 1)
        let size = text.boundingRect(
            with: CGSize(width: width, height: .greatestFiniteMagnitude), options: [.usesLineFragmentOrigin, .usesFontLeading]
        ).size
        let box = CGRect(x: rect.midX - width / 2, y: rect.midY - ceil(size.height) / 2, width: width, height: ceil(size.height))
        text.draw(with: box, options: [.usesLineFragmentOrigin, .usesFontLeading])
    }

    /// Free text: left-aligned at the shape's origin, centred on its height.
    private func drawText(_ label: String?, in rect: CGRect, bold: Bool) {
        guard let label, !label.isEmpty else { return }
        let text = NSAttributedString(string: label, attributes: labelAttributes(bold: bold, alignment: .left))
        let size = text.boundingRect(
            with: CGSize(width: CGFloat.greatestFiniteMagnitude, height: .greatestFiniteMagnitude),
            options: [.usesLineFragmentOrigin, .usesFontLeading]
        ).size
        let y = rect.minY + max(0, (rect.height - size.height) / 2)
        text.draw(
            with: CGRect(x: rect.minX, y: y, width: ceil(size.width) + 1, height: ceil(size.height)),
            options: [.usesLineFragmentOrigin, .usesFontLeading]
        )
    }

    /// A small label centred on `point`, on a page-coloured plate that hides
    /// the line behind it.
    private func drawKnockoutLabel(_ label: String, at point: CGPoint) {
        let text = NSAttributedString(string: label, attributes: labelAttributes(bold: false, alignment: .center, size: Self.smallFontSize))
        let size = text.boundingRect(
            with: CGSize(width: CGFloat.greatestFiniteMagnitude, height: .greatestFiniteMagnitude),
            options: [.usesLineFragmentOrigin, .usesFontLeading]
        ).size
        let textRect = CGRect(
            x: point.x - ceil(size.width) / 2, y: point.y - ceil(size.height) / 2,
            width: ceil(size.width), height: ceil(size.height)
        )
        let plate = textRect.insetBy(dx: -4, dy: -1)
        context.setFillColor(palette.pageBackground.cgColor)
        context.addPath(CGPath(roundedRect: plate, cornerWidth: 3, cornerHeight: 3, transform: nil))
        context.fillPath()
        text.draw(with: textRect, options: [.usesLineFragmentOrigin, .usesFontLeading])
    }
}

/// A filled triangular arrowhead.
struct ArrowHead {
    var length: CGFloat
    var halfWidth: CGFloat

    init(thick: Bool) {
        length = thick ? 11 : 9
        halfWidth = thick ? 4.5 : 3.5
    }

    /// Where the shaft should stop so its end hides under the head.
    func base(tip: CGPoint, from tail: CGPoint) -> CGPoint {
        guard let direction = tail.direction(to: tip), tail.distance(to: tip) > length else { return tip }
        return tip.offset(by: direction, distance: -(length - 1))
    }

    func path(tip: CGPoint, from tail: CGPoint) -> CGPath {
        let path = CGMutablePath()
        guard let direction = tail.direction(to: tip) else { return path }
        let base = tip.offset(by: direction, distance: -length)
        let normal = CGPoint(x: -direction.y, y: direction.x)
        path.move(to: tip)
        path.addLine(to: base.offset(by: normal, distance: halfWidth))
        path.addLine(to: base.offset(by: normal, distance: -halfWidth))
        path.closeSubpath()
        return path
    }
}

extension CGPoint {
    func distance(to other: CGPoint) -> CGFloat {
        hypot(other.x - x, other.y - y)
    }

    /// The unit vector from here to `other`, or nil if they coincide.
    func direction(to other: CGPoint) -> CGPoint? {
        let length = distance(to: other)
        guard length > 0.0001 else { return nil }
        return CGPoint(x: (other.x - x) / length, y: (other.y - y) / length)
    }

    func offset(by direction: CGPoint, distance: CGFloat) -> CGPoint {
        CGPoint(x: x + direction.x * distance, y: y + direction.y * distance)
    }

    func interpolated(to other: CGPoint, fraction: CGFloat) -> CGPoint {
        CGPoint(x: x + (other.x - x) * fraction, y: y + (other.y - y) * fraction)
    }
}
