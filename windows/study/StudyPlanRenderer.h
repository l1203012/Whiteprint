#pragma once
// Port of StudyPlanRenderer.swift.
#include "core/Note.h"
#include "core/StudyModel.h"

namespace wp::StudyPlanRenderer {

/// The study plan as a Whiteprint note: overview, learning path with ★/○/✕ tiers and refs, a `- [ ]`
/// to-do checklist, a flashcard deck, and diagrams as drawings.
///
/// Page 1 holds the overview and learning path, then come pages for the to-do list, the flashcards and
/// the diagrams; pages with nothing to show are left out.
Note note(const StudyPlan &plan);

} // namespace wp::StudyPlanRenderer
