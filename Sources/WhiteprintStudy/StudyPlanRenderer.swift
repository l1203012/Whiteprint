import Foundation
import WhiteprintCore

public enum StudyPlanRenderer {
    /// The study plan as a Whiteprint note: overview, learning path with ★/○/✕
    /// tiers and refs, a `- [ ]` to-do checklist, a flashcard deck, and
    /// diagrams as drawings.
    ///
    /// Page 1 holds the overview and learning path, then come pages for the
    /// to-do list, the flashcards and the diagrams; pages with nothing to show
    /// are left out.
    public static func note(for plan: StudyPlan) -> Note {
        let title = oneLine(plan.title).isEmpty ? "Study plan" : oneLine(plan.title)
        // One text block per page: adjacent text blocks would merge when the note is read back.
        var pages = [NotePage(blocks: [.text(overview(plan, title: title) + "\n\n" + learningPath(plan.modules))])]
        if !plan.tasks.isEmpty {
            pages.append(NotePage(blocks: [.text(toDo(plan.tasks))]))
        }
        let cards = plan.flashcards.filter { !oneLine($0.question).isEmpty || !oneLine($0.answer).isEmpty }
        if !cards.isEmpty {
            pages.append(NotePage(blocks: [.text("## Flashcards"), .cards(CardDeck(title: title, cards: cards))]))
        }
        let diagrams = plan.diagrams
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { !$0.isEmpty }
        if !diagrams.isEmpty {
            pages.append(NotePage(blocks: [.text("## Diagrams")] + diagrams.map { .drawing(Drawing(id: "", source: $0)) }))
        }
        return Note(title: title, pages: pages)
    }
}

private extension StudyPlanRenderer {
    static let legend = "★ must know · ○ good to know · ✕ can skip"

    static func overview(_ plan: StudyPlan, title: String) -> String {
        var header = "## Overview"
        let total = plan.modules.compactMap(\.minutes).filter { $0 > 0 }.reduce(0, +)
        if total > 0 {
            header += " · ≈ \(duration(total)) in total"
        }
        var lines = ["# \(title)", "", header]
        let text = escaped(plan.overview.trimmingCharacters(in: .whitespacesAndNewlines))
        if !text.isEmpty {
            lines += ["", text]
        }
        lines += ["", legend]
        return lines.joined(separator: "\n")
    }

    static func learningPath(_ modules: [StudyModule]) -> String {
        var sections = ["## Learning path"]
        for (i, module) in modules.enumerated() {
            var heading = "### \(i + 1). \(oneLine(module.title))"
            if let minutes = module.minutes, minutes > 0 {
                heading += " · ≈ \(duration(minutes))"
            }
            let points = module.points.filter { !oneLine($0.text).isEmpty }
            let listed = points.filter { $0.importance == .must } + points.filter { $0.importance == .good }
            var section = [heading]
            if !listed.isEmpty {
                section.append(listed.map(item).joined(separator: "\n"))
            }
            let skipped = points.filter { $0.importance == .skip }
            if !skipped.isEmpty {
                section.append("✕ Can skip: " + skipped.map { "\(oneLine($0.text))\(refSuffix($0.ref, parenthesised: true))" }.joined(separator: "; "))
            }
            sections.append(section.joined(separator: "\n\n"))
        }
        return sections.joined(separator: "\n\n")
    }

    static func item(_ point: StudyPoint) -> String {
        let text = oneLine(point.text)
        let body = point.importance == .must ? "★ **\(text)**" : "○ \(text)"
        return "- \(body)\(refSuffix(point.ref, parenthesised: false))"
    }

    static func refSuffix(_ ref: String?, parenthesised: Bool) -> String {
        guard let ref = ref.map(oneLine), !ref.isEmpty else { return "" }
        return parenthesised ? " *(\(ref))*" : " — *\(ref)*"
    }

    static func toDo(_ tasks: [StudyTask]) -> String {
        let lines = sortedByDue(tasks).map { task -> String in
            var line = "- [ ] \(oneLine(task.text))"
            if let due = task.due.map(oneLine), !due.isEmpty {
                line += " — due \(due)"
            }
            return line + refSuffix(task.ref, parenthesised: false)
        }
        return (["## To-do", ""] + lines).joined(separator: "\n")
    }

    /// Tasks with a calendar date first (earliest first), then `week N` ones,
    /// then the rest in their original order.
    static func sortedByDue(_ tasks: [StudyTask]) -> [StudyTask] {
        let keyed = tasks.enumerated().map { i, task -> (key: (Int, Double), index: Int, task: StudyTask) in
            let due = task.due.map(oneLine) ?? ""
            if let date = date(from: due) {
                return ((0, date.timeIntervalSinceReferenceDate), i, task)
            }
            if let week = week(from: due) {
                return ((1, Double(week)), i, task)
            }
            return ((2, 0), i, task)
        }
        return keyed.sorted { a, b in a.key != b.key ? a.key < b.key : a.index < b.index }.map(\.task)
    }

    static let dateFormatters: [DateFormatter] = [
        "yyyy-MM-dd", "dd.MM.yyyy", "d.M.yyyy", "dd/MM/yyyy", "d MMMM yyyy", "d MMM yyyy", "MMMM d, yyyy", "MMM d, yyyy",
    ].map { format in
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.timeZone = TimeZone(identifier: "UTC")
        formatter.dateFormat = format
        formatter.isLenient = false
        return formatter
    }

    static func date(from text: String) -> Date? {
        for formatter in dateFormatters {
            if let date = formatter.date(from: text) { return date }
        }
        // A date at the start, e.g. `2026-01-15 23:59` or `2026-01-15 (noon)`.
        if let first = text.split(separator: " ").first, first.count < text.count {
            return date(from: String(first))
        }
        return nil
    }

    /// `week 6`, `Week 6`, `wk 6`.
    static func week(from text: String) -> Int? {
        let words = text.lowercased().split(separator: " ")
        guard words.count >= 2, ["week", "wk"].contains(words[0]) else { return nil }
        return Int(words[1].trimmingCharacters(in: .punctuationCharacters))
    }

    static func duration(_ minutes: Int) -> String {
        let h = minutes / 60, m = minutes % 60
        if h == 0 { return "\(m) min" }
        return m == 0 ? "\(h) h" : "\(h) h \(m) min"
    }

    /// Keeps free text from turning into note structure: a `+++page` line or a
    /// code fence (which could open a drawing or swallow the rest of the page).
    static func escaped(_ text: String) -> String {
        text.split(separator: "\n", omittingEmptySubsequences: false).map { line -> String in
            let trimmed = line.drop { $0 == " " }
            let isStructure = trimmed.hasPrefix("+++") || trimmed.hasPrefix("```") || trimmed.hasPrefix("~~~")
            return isStructure ? "\\" + trimmed : String(line)
        }.joined(separator: "\n")
    }
}
