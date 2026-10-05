extension WhiteprintText {
    /// Instructions for building a study plan, served as MCP prompt `study_plan`
    /// and passed to `claude -p` by the one-click button (followed by an
    /// `Imports: i1, i2` line).
    public static let studyPlanPrompt = """
    Make a study plan from course material imported into Whiteprint, using the whiteprint tools.

    1. Call list_imports. Use the imports named under "Imports" below, or all of them.
    2. For every chunk of each import that isn't done yet: read_chunk, then save_points for that chunk \
    (an empty list if it has nothing useful). Don't skip chunks.
    Points are short lines in your own words. importance:
    - must: definitions, core concepts and mechanisms, formulas, anything emphasised or likely to be examined
    - good: supporting detail
    - skip: examples, anecdotes, history, repetition, admin, filler
    ref: the [ref] marker above the source line, copied exactly (e.g. "slide 14").
    topic: a short subject label, worded the same across chunks.
    Assignments, exercises, readings and deadlines: topic "task", due date in the text.
    3. Call get_points, then build_study_plan once:
    - overview: 5–10 lines on what the material covers
    - modules in a good learning order (foundations first), each with realistic minutes and its points; \
    merge duplicates, keep refs exactly as get_points shows them
    - tasks from the "task" points, with due and ref
    - diagrams: 0–3 small concept maps or process flows in the drawing language, only where they really help, \
    under 15 lines each (read whiteprint://dsl if unsure)
    - flashcards: 10–30, mostly on must points; one fact per card, question ≤ 15 words, answer ≤ 30 words, ref copied exactly

    Write in the material's language. Never repeat chunk text back. Finish with one short line.
    """

    /// Instructions for making flashcards from a note or an import, served as
    /// MCP prompt `flashcards` (followed by a `Source: n3` line).
    public static let flashcardsPrompt = """
    Make flashcards in Whiteprint from the source named under "Source" below, using the whiteprint tools.

    1. A note (n…): read_note. An import (i…): get_points for it; if it has none yet, read_chunk every chunk \
    (list_imports shows how many).
    2. Call create_flashcards once with a short title and 10–30 cards on the most important facts, definitions and formulas:
    one fact per card, question ≤ 15 words, answer ≤ 30 words, ref = the source's ref copied exactly (leave it out if none).
    For a note, add the deck to that note; for an import, leave note out to get a new note.

    Write in the source's language. Finish with one short line.
    """
}
