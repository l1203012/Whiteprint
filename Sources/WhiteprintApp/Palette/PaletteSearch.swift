import Foundation

/// Ranks command palette entries against what's typed: a prefix beats a
/// word start, which beats a substring, which beats a scattered subsequence.
enum PaletteSearch {
    /// Higher is better; nil when `query` doesn't match `title` at all.
    static func score(_ title: String, query: String) -> Int? {
        let query = query.trimmingCharacters(in: .whitespaces).lowercased()
        guard !query.isEmpty else { return 0 }
        let title = title.lowercased()
        if title.hasPrefix(query) { return 400 - title.count }
        if let range = title.range(of: query) {
            let atWordStart = range.lowerBound == title.startIndex
                || !title[title.index(before: range.lowerBound)].isLetter
            return (atWordStart ? 300 : 200) - title.count
        }
        var remaining = query[...]
        for character in title where character == remaining.first {
            remaining = remaining.dropFirst()
            if remaining.isEmpty { return 100 - title.count }
        }
        return nil
    }

    /// The entries matching `query`, best first; ties keep their order.
    static func filter<T>(_ entries: [T], query: String, title: (T) -> String) -> [T] {
        entries.enumerated()
            .compactMap { offset, entry in score(title(entry), query: query).map { (entry, $0, offset) } }
            .sorted { ($0.1, -$0.2) > ($1.1, -$1.2) }
            .map(\.0)
    }
}
