#pragma once
// Port of SettingsWindowController.swift: tabs AI, Appearance, Notes, Study.
#include <QWidget>

class QTabBar;
class QStackedWidget;

namespace wp {

class SettingsWindow : public QWidget {
    Q_OBJECT
public:
    /// Shows (creating on first use) the one settings window and raises it.
    static void showShared();

    explicit SettingsWindow(QWidget *parent = nullptr);

    int currentTab() const;
    void setCurrentTab(int index);
    /// The tab pages, for tests: 0 AI, 1 Appearance, 2 Notes, 3 Study.
    QWidget *page(int index) const;

private:
    QTabBar *m_tabs;
    QStackedWidget *m_pages;
};

} // namespace wp
