#pragma once
#include "editor/EditorDocument.h"
#include "render/DrawingView.h"
#include "render/Palette.h"

#include <QFrame>
#include <functional>

class QLabel;
class QPlainTextEdit;

namespace wp {

class PreviewSwatch;

/// The drawing popup's content: a monospaced source editor, a live
/// preview on a blueprint swatch and the compile errors. Ctrl+Enter commits;
/// the popup commits on any close (clicking outside, Esc).
class DrawingSourceEditor : public QFrame
{
    Q_OBJECT

public:
    static constexpr int editorWidth = 460;

    DrawingSourceEditor(BlockID blockID, const QString &source, const BlueprintPalette &palette,
                        std::function<void(const QString &)> onCommit, QWidget *parent = nullptr);

    BlockID blockID() const { return m_blockID; }
    QString source() const;
    /// The compile errors shown (one per line).
    QString errorsText() const;

    /// Shows the editor as a popup below `anchor`.
    void present(QWidget *anchor);
    /// Closes the popup (which commits), or commits directly when not shown.
    void close();
    /// Closes without committing (the drawing went away).
    void discard();
    void commit();

protected:
    void hideEvent(QHideEvent *) override;

private:
    void update_();

    BlockID m_blockID;
    BlueprintPalette m_palette;
    std::function<void(const QString &)> m_onCommit;
    QPlainTextEdit *m_source;
    PreviewSwatch *m_preview;
    QLabel *m_errors;
    bool m_committed = false;
};

} // namespace wp
