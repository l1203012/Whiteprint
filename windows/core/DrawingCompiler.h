#pragma once
#include "core/DSLParser.h"
#include "core/DrawingScene.h"

#include <QString>

namespace wp {

/// Compiles drawing language source into a `DrawingScene`.
///
/// The language is described in `docs/DSL.md`. Compiling never fails as a
/// whole: statements with errors are skipped and reported, the rest is drawn.
namespace DrawingCompiler {

CompiledDrawing compile(const QString &source);

} // namespace DrawingCompiler

/// Default sizes, in grid units.
namespace DrawingDefaults {

inline const GridSize boxSize{10, 4};
inline const GridSize circleSize{4, 4};
inline const GridSize dbSize{8, 5};
inline constexpr double gap = 4.0;
inline constexpr double groupPadding = 1.5;
inline constexpr double groupLabelHeight = 2.0;
/// Rough label character width and line height, used to grow shapes so
/// their labels fit. The renderer measures text exactly.
inline constexpr double characterWidth = 0.8;
inline constexpr double lineHeight = 2.0;

GridSize size(ShapeKind kind, const std::optional<QString> &label);

} // namespace DrawingDefaults

} // namespace wp
