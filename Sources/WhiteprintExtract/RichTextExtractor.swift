import AppKit

/// Reads Word documents with AppKit's importers and splits them into sections
/// at heading-like paragraphs.
enum RichTextExtractor {
    /// Sections longer than this are split at paragraph boundaries.
    static let maxSectionLength = 6_000
    /// Headings in refs are cut to this many characters.
    static let maxHeadingLength = 60

    struct Paragraph: Equatable {
        var text: String
        var isHeading: Bool
    }

    static func extract(_ url: URL, type: NSAttributedString.DocumentType) throws -> [ExtractedUnit] {
        let paragraphs: [Paragraph] = try autoreleasepool {
            guard let string = try? NSAttributedString(url: url, options: [.documentType: type], documentAttributes: nil) else {
                throw ExtractionError.unreadable(url.lastPathComponent)
            }
            return self.paragraphs(string)
        }
        return sections(paragraphs)
    }

    /// Paragraphs with heading detection: bold or larger than the body font,
    /// or a short title-like line followed by a long body paragraph.
    static func paragraphs(_ string: NSAttributedString) -> [Paragraph] {
        let nsString = string.string as NSString
        var raw: [(text: String, bold: Bool, size: CGFloat)] = []
        var sizeWeights: [CGFloat: Int] = [:]
        nsString.enumerateSubstrings(in: NSRange(location: 0, length: nsString.length), options: .byParagraphs) { substring, range, _, _ in
            let text = TextCleaner.collapseWhitespace(substring ?? "")
            guard !text.isEmpty else { return }
            var allBold = true
            var largest: CGFloat = 0
            string.enumerateAttribute(.font, in: range) { value, run, _ in
                guard let font = value as? NSFont else { allBold = false; return }
                let runText = nsString.substring(with: run)
                guard runText.contains(where: { !$0.isWhitespace }) else { return }
                if !font.fontDescriptor.symbolicTraits.contains(.bold) { allBold = false }
                largest = max(largest, font.pointSize)
                sizeWeights[font.pointSize, default: 0] += run.length
            }
            raw.append((text, allBold, largest))
        }
        let bodySize = sizeWeights.max { $0.value < $1.value }?.key ?? 12

        return raw.indices.map { i in
            let (text, bold, size) = raw[i]
            let next = raw.indices.contains(i + 1) ? raw[i + 1].text : ""
            let titleLike = isTitleLike(text)
            let styled = titleLike && (bold || size >= bodySize + 1.5)
            let shortBeforeBody = titleLike && text.count <= 60 && next.count >= 150
            return Paragraph(text: text, isHeading: styled || shortBeforeBody)
        }
    }

    private static func isTitleLike(_ text: String) -> Bool {
        guard text.count <= 120, text.split(separator: " ").count <= 14, let last = text.last else { return false }
        if ".,;:!?".contains(last) && !text.hasSuffix("?") { return false }
        if let first = text.first, "•‣◦▪-–*".contains(first) { return false }
        return text.first?.isLowercase != true
    }

    /// Groups paragraphs into sections that start at headings; stacked
    /// headings share a section. Long sections are split at paragraphs.
    static func sections(_ paragraphs: [Paragraph]) -> [ExtractedUnit] {
        var groups: [(heading: String?, lines: [String], hasBody: Bool)] = []
        for paragraph in paragraphs {
            if paragraph.isHeading {
                if let last = groups.last, last.heading != nil, !last.hasBody {
                    groups[groups.count - 1].lines.append(paragraph.text)
                } else {
                    groups.append((paragraph.text, [paragraph.text], false))
                }
            } else {
                if groups.isEmpty { groups.append((nil, [], false)) }
                groups[groups.count - 1].lines.append(paragraph.text)
                groups[groups.count - 1].hasBody = true
            }
        }

        var units: [ExtractedUnit] = []
        for group in groups {
            for (part, text) in split(group.lines).enumerated() {
                let ref: String
                if let heading = group.heading {
                    ref = "§ " + truncated(heading) + (part > 0 ? ", part \(part + 1)" : "")
                } else {
                    ref = "section \(units.count + 1)"
                }
                units.append(ExtractedUnit(ref: ref, text: text))
            }
        }
        return units
    }

    private static func split(_ lines: [String]) -> [String] {
        var parts: [String] = []
        var current: [String] = []
        var length = 0
        for line in lines {
            if !current.isEmpty && length + 1 + line.count > maxSectionLength {
                parts.append(current.joined(separator: "\n"))
                current = []
                length = 0
            }
            length += (current.isEmpty ? 0 : 1) + line.count
            current.append(line)
        }
        if !current.isEmpty { parts.append(current.joined(separator: "\n")) }
        return parts
    }

    private static func truncated(_ heading: String) -> String {
        guard heading.count > maxHeadingLength else { return heading }
        return heading.prefix(maxHeadingLength - 1).trimmingCharacters(in: .whitespaces) + "…"
    }
}
