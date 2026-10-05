#pragma once
// Port of WelcomeNote.swift.
#include "core/Note.h"

#include <QString>

namespace wp {

/// The short tour created when the notes folder is empty on launch.
namespace WelcomeNote {

inline const QString title = QStringLiteral("Welcome");

/// The `.wprint` text of the tour (shortcuts are the Windows ones: Ctrl instead of Cmd).
QString source();

/// The parsed tour.
Note note();

} // namespace WelcomeNote

} // namespace wp
