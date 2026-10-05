#include "app/ReferenceWindow.h"

#include "app/MacStyle.h"
#include "render/Fonts.h"

#include <QApplication>
#include <QPlainTextEdit>
#include <QScreen>
#include <QShowEvent>
#include <QVBoxLayout>

namespace wp::ReferenceWindow {

namespace {

class Window : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        mac::styleTitleBar(this);
    }
};

} // namespace

QWidget *make(const QString &title, const QString &text)
{
    auto *window = new Window;
    window->setWindowTitle(title);
    window->setAttribute(Qt::WA_QuitOnClose, false);
    auto *layout = new QVBoxLayout(window);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *view = new QPlainTextEdit;
    view->setReadOnly(true);
    view->setPlainText(text);
    view->setFont(fonts::monospaced(12));
    view->setFrameShape(QFrame::NoFrame);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->document()->setDocumentMargin(16);
    layout->addWidget(view);
    window->resize(620, 560);
    if (const QScreen *screen = QApplication::primaryScreen())
        window->move(screen->availableGeometry().center() - QPoint(310, 280));
    return window;
}

} // namespace wp::ReferenceWindow
