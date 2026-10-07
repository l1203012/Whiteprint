#include "app/SettingsWindow.h"

#include "app/AISettings.h"
#include "app/AppServices.h"
#include "app/MacStyle.h"
#include "app/SettingsPages.h"
#include "app/UiKit.h"
#include "app/ViewPreferences.h"
#include "render/Fonts.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QPainter>
#include <QStyle>
#include <QWindow>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace wp {

namespace {
QPointer<SettingsWindow> sharedWindow;

/// A toolbar-style tab: icon above its label, the selected one on a rounded grey backdrop.
class ToolbarTab : public QToolButton {
public:
    ToolbarTab(const QString &label, ui::Symbol symbol) : m_symbol(symbol)
    {
        setText(label);
        setCheckable(true);
        setAutoExclusive(true);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(76, 54);
        setFont(fonts::system(11));
        setCursor(Qt::ArrowCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto c = mac::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (isChecked()) {
            p.setPen(Qt::NoPen);
            p.setBrush(mac::isDark() ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 22));
            p.drawRoundedRect(QRectF(rect()).adjusted(4, 2, -4, -2), 7, 7);
        }
        const QColor tint = isChecked() ? c.accent : c.secondaryText;
        ui::drawSymbol(p, m_symbol, QRectF(width() / 2.0 - 12, 7, 24, 24), tint);
        p.setFont(font());
        p.setPen(isChecked() ? c.accent : c.text);
        p.drawText(QRectF(0, 33, width(), 16), Qt::AlignHCenter | Qt::AlignVCenter, text());
    }

private:
    ui::Symbol m_symbol;
};
} // namespace

void SettingsWindow::showShared()
{
    if (!sharedWindow) {
        sharedWindow = new SettingsWindow;
        sharedWindow->setAttribute(Qt::WA_DeleteOnClose);
        sharedWindow->move(QCursor::pos() - QPoint(sharedWindow->width() / 2, 120));
    }
    sharedWindow->show();
    sharedWindow->raise();
    sharedWindow->activateWindow();
}

SettingsWindow::SettingsWindow(QWidget *parent) : QWidget(parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint)
{
    setAutoFillBackground(true);
    auto &services = AppServices::shared();
    auto *ai = new AISettingsPage(AISettings::shared());
    connect(ai, &AISettingsPage::studyStateChanged, this, [&services] { emit services.study().changed(); });
    auto *appearance = new AppearanceSettingsPage(ViewPreferences::shared());
    auto *notes = new NotesSettingsPage(services.library());
    auto *study = new StudySettingsPage(services.study());

    m_pages = new QStackedWidget;
    const QList<std::pair<QString, ui::Symbol>> tabs = {
        {QStringLiteral("AI"), ui::Symbol::sparkles},
        {QStringLiteral("Appearance"), ui::Symbol::palette},
        {QStringLiteral("Notes"), ui::Symbol::folder},
        {QStringLiteral("Study"), ui::Symbol::graduation},
    };
    const QList<QWidget *> pages = {ai, appearance, notes, study};
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(8, 6, 8, 6);
    toolbar->setSpacing(2);
    toolbar->addStretch(1);
    auto *group = new QButtonGroup(this);
    for (int i = 0; i < pages.size(); ++i) {
        auto *scroll = new QScrollArea;
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(pages[i]);
        m_pages->addWidget(scroll);
        auto *tab = new ToolbarTab(tabs[i].first, tabs[i].second);
        group->addButton(tab, i);
        toolbar->addWidget(tab);
        if (i == 0)
            tab->setChecked(true);
    }
    toolbar->addStretch(1);
    connect(group, &QButtonGroup::idClicked, this, [this](int i) { setCurrentTab(i); });

    auto *divider = new QFrame;
    divider->setFixedHeight(1);
    divider->setStyleSheet(QStringLiteral("background: %1;").arg(mac::colors().separator.name()));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    layout->addLayout(toolbar);
    layout->addWidget(divider);
    layout->addWidget(m_pages);
    m_tabs = nullptr;
    setCurrentTab(0);
    mac::styleTitleBar(this);
}

int SettingsWindow::currentTab() const
{
    return m_pages->currentIndex();
}

void SettingsWindow::setCurrentTab(int index)
{
    static const QStringList titles = {QStringLiteral("AI"), QStringLiteral("Appearance"), QStringLiteral("Notes"),
                                       QStringLiteral("Study")};
    if (index < 0 || index >= m_pages->count())
        return;
    for (int i = 0; i < m_pages->count(); ++i)
        m_pages->widget(i)->setSizePolicy(i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored,
                                          i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    m_pages->setCurrentIndex(index);
    QWidget *current = page(index);
    const int pageWidth = current->sizeHint().width();
    const int pageHeight = qMax(current->sizeHint().height(), current->layout() ? current->layout()->totalHeightForWidth(pageWidth) : 0);
    // Cap the height to the screen (minus the toolbar, title bar and some margin); taller pages scroll.
    const QScreen *screen = windowHandle() && windowHandle()->screen() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
    const int available = screen ? screen->availableGeometry().height() - 160 : pageHeight;
    const int height = qMin(pageHeight, qMax(240, available));
    m_pages->setFixedHeight(height);
    m_pages->setFixedWidth(pageWidth + (height < pageHeight ? style()->pixelMetric(QStyle::PM_ScrollBarExtent) : 0));
    setWindowTitle(titles[index]);
    if (auto *group = findChild<QButtonGroup *>())
        if (auto *button = group->button(index))
            button->setChecked(true);
    adjustSize();
}

QWidget *SettingsWindow::page(int index) const
{
    return static_cast<QScrollArea *>(m_pages->widget(index))->widget();
}

} // namespace wp
