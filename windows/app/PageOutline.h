#pragma once
// Port of PageOutline.swift.
#include "core/Note.h"

#include <QString>
#include <QStringList>

namespace wp {

/// Short labels for a note's pages, for the sidebar and the command palette.
namespace PageOutline {

/// The page's first heading (or first line of text), or `Page n` (`number` is 1-based).
QString title(const NotePage &page, int number);

QStringList titles(const Note &note);

} // namespace PageOutline

} // namespace wp
