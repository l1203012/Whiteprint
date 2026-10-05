#pragma once
// macOS-flavoured look for the Windows app: Fusion base, system-blue accent, rounded controls,
// light/dark following the Windows app theme. Shared by every window; extend here, not per window.
#include <QColor>
#include <QString>

class QApplication;
class QWidget;

namespace wp::mac {

struct Colors {
    QColor window, sidebar, control, controlBorder, text, secondaryText, accent, separator, selection, selectionText;
};

// Forces light (0) or dark (1) regardless of Windows; -1 follows Windows again. For tests and screenshots.
void setDarkOverride(int mode);

// True when Windows is in dark app mode (re-evaluated on call).
bool isDark();
Colors colors();

// Installs Fusion, the palette and the application style sheet. Call once after QApplication exists
// and again when the system theme changes.
void apply(QApplication& app);

// The application style sheet for the current colors (also used by apply()).
QString styleSheet();

// Sets a dark/light native title bar (DWM immersive dark mode) and rounded corners where supported.
void styleTitleBar(QWidget* window);

} // namespace wp::mac
