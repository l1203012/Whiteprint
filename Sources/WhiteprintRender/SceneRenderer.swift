import AppKit
import WhiteprintCore

public enum SceneRenderer {
    /// Points per grid unit.
    public static let unit: CGFloat = 10
    /// Space around the scene's bounds, in points.
    public static let margin: CGFloat = 10

    /// Size of the canvas `draw` fills: the scene's bounds plus margins.
    public static func canvasSize(for scene: DrawingScene) -> CGSize {
        let bounds = scene.bounds
        return CGSize(
            width: CGFloat(bounds.size.width) * unit + 2 * margin,
            height: CGFloat(bounds.size.height) * unit + 2 * margin
        )
    }

    /// Draws the scene with its bounds' top-left at (margin, margin).
    /// `context` must be y-down (flipped), as in a flipped NSView.
    public static func draw(_ scene: DrawingScene, in context: CGContext, palette: BlueprintPalette) {
        let bounds = scene.bounds
        context.saveGState()
        context.translateBy(x: margin - CGFloat(bounds.minX) * unit, y: margin - CGFloat(bounds.minY) * unit)
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: context, flipped: true)
        ScenePainter(context: context, palette: palette).draw(scene)
        NSGraphicsContext.restoreGraphicsState()
        context.restoreGState()
    }
}

extension GridPoint {
    /// The point on the canvas, before the scene's margin offset.
    var canvasPoint: CGPoint {
        CGPoint(x: CGFloat(x) * SceneRenderer.unit, y: CGFloat(y) * SceneRenderer.unit)
    }
}

extension GridRect {
    var canvasRect: CGRect {
        CGRect(
            x: CGFloat(minX) * SceneRenderer.unit, y: CGFloat(minY) * SceneRenderer.unit,
            width: CGFloat(size.width) * SceneRenderer.unit, height: CGFloat(size.height) * SceneRenderer.unit
        )
    }
}
