#include "app/SettingsWindow.h"

#include "app/AISettings.h"
#include "app/AppServices.h"
#include "app/MacStyle.h"
#include "app/SettingsPages.h"
#include "app/UiKit.h"
#include "render/Fonts.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPainter>
#include <QPointer>
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
    auto *notes = new NotesSettingsPage(services.library());
    auto *study = new StudySettingsPage(services.study());

    m_pages = new QStackedWidget;
    const QList<std::pair<QString, ui::Symbol>> tabs = {
        {QStringLiteral("AI"), ui::Symbol::sparkles},
        {QStringLiteral("Notes"), ui::Symbol::folder},
        {QStringLiteral("Study"), ui::Symbol::graduation},
    };
    const QList<QWidget *> pages = {ai, notes, study};
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(8, 6, 8, 6);
    toolbar->setSpacing(2);
    toolbar->addStretch(1);
    auto *group = new QButtonGroup(this);
    for (int i = 0; i < pages.size(); ++i) {
        m_pages->addWidget(pages[i]);
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
    static const QStringList titles = {QStringLiteral("AI"), QStringLiteral("Notes"), QStringLiteral("Study")};
    if (index < 0 || index >= m_pages->count())
        return;
    for (int i = 0; i < m_pages->count(); ++i)
        m_pages->widget(i)->setSizePolicy(i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored,
                                          i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    m_pages->setCurrentIndex(index);
    QWidget *current = m_pages->widget(index);
    const int pageHeight = qMax(current->sizeHint().height(), current->layout() ? current->layout()->totalHeightForWidth(current->width()) : 0);
    m_pages->setFixedHeight(pageHeight);
    setWindowTitle(titles[index]);
    if (auto *group = findChild<QButtonGroup *>())
        if (auto *button = group->button(index))
            button->setChecked(true);
    adjustSize();
}

QWidget *SettingsWindow::page(int index) const
{
    return m_pages->widget(index);
}

} // namespace wp
