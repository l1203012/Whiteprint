#pragma once
#include "core/DrawingScene.h"
#include "render/Palette.h"

#include <QList>
#include <QString>
#include <QWidget>

namespace wp {

/// Renders drawing-language source. Sized to its content, transparent
/// background so the page grid shows through. Recompiles when the source changes.
class DrawingView : public QWidget
{
public:
    /// Keeps an empty drawing tall enough to click.
    static constexpr int minimumHeight = 40;

    explicit DrawingView(const QString &source = QString(), BlueprintPalette palette = BlueprintPalette::blueprint(),
                         QWidget *parent = nullptr);

    const QString &source() const { return m_source; }
    void setSource(const QString &source);
    const BlueprintPalette &palette() const { return m_palette; }
    void setPalette(const BlueprintPalette &palette);
    /// Errors from the last compile.
    const QList<DrawingError> &errors() const { return m_errors; }
    /// The scene from the last compile.
    const DrawingScene &scene() const { return m_scene; }

    /// The canvas size, at least `minimumHeight` tall.
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void compile();

    QString m_source;
    BlueprintPalette m_palette;
    QList<DrawingError> m_errors;
    DrawingScene m_scene;
};

} // namespace wp
