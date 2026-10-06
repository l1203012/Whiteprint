#include "app/MacStyle.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>
#include <QWidget>
#include <windows.h>

namespace wp::mac {

static int g_darkOverride = -1;
void setDarkOverride(int mode) { g_darkOverride = mode; }

bool isDark() {
    if (g_darkOverride >= 0) return g_darkOverride == 1;
    QSettings s("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                QSettings::NativeFormat);
    return s.value("AppsUseLightTheme", 1).toInt() == 0;
}

Colors colors() {
    if (isDark()) {
        return {QColor("#1e1e1e"), QColor("#2a2a2c"), QColor("#3a3a3c"), QColor("#545458"), QColor("#f2f2f7"),
                QColor("#98989f"), QColor("#0a84ff"), QColor("#38383a"), QColor("#0a84ff"), QColor("#ffffff")};
    }
    return {QColor("#ffffff"), QColor("#ececf0"), QColor("#ffffff"), QColor("#c6c6c8"), QColor("#1d1d1f"),
            QColor("#6e6e73"), QColor("#007aff"), QColor("#d1d1d6"), QColor("#007aff"), QColor("#ffffff")};
}

QString styleSheet() {
    const Colors c = colors();
    return QString(R"(
QToolTip { background: %1; color: %2; border: 1px solid %3; padding: 4px 6px; border-radius: 6px; }
QPushButton { background: %4; color: %2; border: 1px solid %3; border-radius: 6px; padding: 4px 14px; }
QPushButton:hover { border-color: %5; }
QPushButton:default { background: %5; color: %6; border-color: %5; }
QLineEdit, QComboBox, QSpinBox { background: %4; color: %2; border: 1px solid %3; border-radius: 6px; padding: 3px 6px; selection-background-color: %5; selection-color: %6; }
QLineEdit:focus, QComboBox:focus { border: 2px solid %5; }
QMenu { background: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 5px 22px 5px 12px; border-radius: 4px; }
QMenu::item:selected { background: %5; color: %6; }
QMenu::separator { height: 1px; background: %7; margin: 4px 8px; }
QScrollBar:vertical { width: 10px; background: transparent; }
QScrollBar::handle:vertical { background: %3; border-radius: 4px; min-height: 30px; margin: 2px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar:horizontal { height: 10px; background: transparent; }
QScrollBar::handle:horizontal { background: %3; border-radius: 4px; min-width: 30px; margin: 2px; }
)")
        .arg(c.window.name(), c.text.name(), c.controlBorder.name(), c.control.name(), c.accent.name(),
             c.selectionText.name(), c.separator.name());
}

void apply(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    const Colors c = colors();
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.control);
    p.setColor(QPalette::AlternateBase, c.sidebar);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.control);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::Highlight, c.selection);
    p.setColor(QPalette::HighlightedText, c.selectionText);
    p.setColor(QPalette::PlaceholderText, c.secondaryText);
    p.setColor(QPalette::ToolTipBase, c.window);
    p.setColor(QPalette::ToolTipText, c.text);
    app.setPalette(p);
    app.setStyleSheet(styleSheet());
}

void styleTitleBar(QWidget* window) {
    if (!window) return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL dark = isDark() ? TRUE : FALSE;
    using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static const auto fn = reinterpret_cast<Fn>(GetProcAddress(LoadLibraryW(L"dwmapi.dll"), "DwmSetWindowAttribute"));
    if (!fn) return;
    fn(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    const DWORD round = 2; // DWMWCP_ROUND
    fn(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &round, sizeof(round));
}

} // namespace wp::mac
