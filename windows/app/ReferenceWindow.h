#pragma once
// Port of ReferenceWindow.swift: a small read-only text window, used for the drawing language reference.
#include <QString>

class QWidget;

namespace wp::ReferenceWindow {

/// A hidden-on-close window with `text` in a monospaced, read-only view (620 x 560).
QWidget *make(const QString &title, const QString &text);

} // namespace wp::ReferenceWindow
