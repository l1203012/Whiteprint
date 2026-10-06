#include "editor/DrawingSourceEditor.h"

#include "core/DrawingCompiler.h"
#include "editor/DrawingBlockView.h"
#include "render/Fonts.h"

#include <QKeyEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <cmath>

namespace wp {

/// A blueprint swatch that centres the preview drawing, scaled to fit.
class PreviewSwatch : public QWidget
{
public:
    PreviewSwatch(const QString &source, const BlueprintPalette &palette, QWidget *parent)
        : QWidget(parent), m_palette(palette), m_drawing(source, palette)
    {
        setFixedHeight(180);
        setSource(source);
    }

    void setSource(const QString &source)
    {
        m_drawing.setSource(source);
        m_canvas = DrawingBlockView::canvasSize(source);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(m_palette.pageBackground);
        p.drawRoundedRect(QRectF(rect()), 6, 6);
        if (m_canvas.width() <= 0 || m_canvas.height() <= 0)
            return;
        const QRectF area = QRectF(rect()).adjusted(8, 8, -8, -8);
        const double scale = std::min({1.0, area.width() / m_canvas.width(), area.height() / m_canvas.height()});
        p.translate((width() - m_canvas.width() * scale) / 2, (height() - m_canvas.height() * scale) / 2);
        p.scale(scale, scale);
        m_drawing.resize(m_canvas.toSize());
        m_drawing.render(&p, QPoint(), QRegion(), QWidget::DrawWindowBackground);
    }

private:
    BlueprintPalette m_palette;
    DrawingView m_drawing;
    QSizeF m_canvas;
};

namespace {

/// Ctrl+Enter commits instead of inserting a newline.
class SourceTextView : public QPlainTextEdit
{
public:
    std::function<void()> onCommit;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && (event->modifiers() & Qt::ControlModifier)) {
            if (onCommit)
                onCommit();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }
};

} // namespace

DrawingSourceEditor::DrawingSourceEditor(BlockID blockID, const QString &source, const BlueprintPalette &palette,
                                         std::function<void(const QString &)> onCommit, QWidget *parent)
    : QFrame(parent, Qt::Popup), m_blockID(blockID), m_palette(palette), m_onCommit(std::move(onCommit))
{
    setFrameShape(QFrame::StyledPanel);
    setFixedWidth(editorWidth);
    auto *title = new QLabel(QStringLiteral("Drawing"));
    title->setFont(fonts::system(13, QFont::DemiBold));
    auto *hint = new QLabel(QStringLiteral("Ctrl+Enter to apply"));
    hint->setFont(fonts::system(11));
    hint->setEnabled(false);
    auto *header = new QHBoxLayout;
    header->addWidget(title);
    header->addStretch();
    header->addWidget(hint);

    auto *edit = new SourceTextView;
    m_source = edit;
    m_source->setFont(fonts::monospaced(12.5));
    m_source->setPlainText(source);
    m_source->setFixedHeight(150);
    m_source->setAccessibleName(QStringLiteral("Drawing source"));
    edit->onCommit = [this] { close(); };
    connect(m_source, &QPlainTextEdit::textChanged, this, [this] { update_(); });

    m_preview = new PreviewSwatch(source, palette, this);

    m_errors = new QLabel;
    m_errors->setFont(fonts::monospaced(11));
    m_errors->setStyleSheet(QStringLiteral("color: #e08a00;"));
    m_errors->setWordWrap(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    layout->addLayout(header);
    layout->addWidget(m_source);
    layout->addWidget(m_preview);
    layout->addWidget(m_errors);
    update_();
}

QString DrawingSourceEditor::source() const
{
    return m_source->toPlainText();
}

QString DrawingSourceEditor::errorsText() const
{
    return m_errors->text();
}

void DrawingSourceEditor::update_()
{
    const QString text = source();
    m_preview->setSource(text);
    QStringList errors;
    for (const DrawingError &error : DrawingCompiler::compile(text).errors)
        errors << error.description();
    m_errors->setText(errors.join(QLatin1Char('\n')));
    m_errors->setVisible(!errors.isEmpty());
}

void DrawingSourceEditor::present(QWidget *anchor)
{
    adjustSize();
    move(anchor->mapToGlobal(QPoint(std::max(0, (anchor->width() - width()) / 2), anchor->height() + 4)));
    show();
    m_source->setFocus();
}

void DrawingSourceEditor::close()
{
    if (isVisible())
        hide(); // hideEvent commits
    else
        commit();
}

void DrawingSourceEditor::discard()
{
    m_committed = true;
    hide();
}

void DrawingSourceEditor::commit()
{
    if (m_committed)
        return;
    m_committed = true;
    if (m_onCommit)
        m_onCommit(source());
}

void DrawingSourceEditor::hideEvent(QHideEvent *event)
{
    QFrame::hideEvent(event);
    commit();
}

} // namespace wp
