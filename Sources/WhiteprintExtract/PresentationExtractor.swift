import Foundation

/// Reads PowerPoint (`.pptx`) slides and speaker notes straight from the
/// package, one slide at a time.
enum PresentationExtractor {
    static func extract(_ url: URL) throws -> [ExtractedUnit] {
        let archive: ZipArchive
        do {
            archive = try ZipArchive(url: url)
        } catch {
            throw ExtractionError.unreadable(url.lastPathComponent)
        }
        let slides = slidePaths(in: archive)
        guard !slides.isEmpty else { throw ExtractionError.unreadable(url.lastPathComponent) }
        return try slides.enumerated().map { number, path in
            try autoreleasepool {
                do {
                    return ExtractedUnit(ref: "slide \(number + 1)", text: try slideText(path, in: archive))
                } catch {
                    throw ExtractionError.unreadable(url.lastPathComponent)
                }
            }
        }
    }

    /// Slide part names in presentation order, from `ppt/presentation.xml`;
    /// falls back to `ppt/slides/slideN.xml` in numeric order.
    static func slidePaths(in archive: ZipArchive) -> [String] {
        if let ordered = try? orderedSlidePaths(in: archive), !ordered.isEmpty { return ordered }
        return archive.entries.map(\.name)
            .compactMap { name -> (Int, String)? in
                guard name.hasPrefix("ppt/slides/slide"), name.hasSuffix(".xml"),
                      let number = Int(name.dropFirst("ppt/slides/slide".count).dropLast(4)) else { return nil }
                return (number, name)
            }
            .sorted { $0.0 < $1.0 }
            .map(\.1)
    }

    private static func orderedSlidePaths(in archive: ZipArchive) throws -> [String] {
        guard let presentation = try archive.contents(of: "ppt/presentation.xml"),
              let rels = try archive.contents(of: "ppt/_rels/presentation.xml.rels") else { return [] }
        let targets = Dictionary(RelationshipParser.parse(rels).map { ($0.id, $0) }) { first, _ in first }
        return SlideListParser.parse(presentation).compactMap { id in
            guard let rel = targets[id] else { return nil }
            let path = PackagePath.resolve(rel.target, relativeTo: "ppt/presentation.xml")
            return archive.entry(named: path) == nil ? nil : path
        }
    }

    /// The slide's title first, then its other text, one line per paragraph,
    /// then the speaker notes as a `Notes:` paragraph.
    static func slideText(_ path: String, in archive: ZipArchive) throws -> String {
        guard let xml = try archive.contents(of: path) else { return "" }
        let shapes = try ShapeTextParser.parse(xml, skipping: ["sldNum", "ftr", "dt", "hdr"])
        let titles = shapes.filter { $0.placeholder == "title" || $0.placeholder == "ctrTitle" }
        let others = shapes.filter { $0.placeholder != "title" && $0.placeholder != "ctrTitle" }
        var text = (titles + others).flatMap(\.paragraphs).joined(separator: "\n")

        let notes = try notesText(for: path, in: archive)
        if !notes.isEmpty {
            text += (text.isEmpty ? "" : "\n\n") + "Notes: " + notes
        }
        return text
    }

    private static func notesText(for slidePath: String, in archive: ZipArchive) throws -> String {
        let directory = (slidePath as NSString).deletingLastPathComponent
        let relsPath = directory + "/_rels/" + (slidePath as NSString).lastPathComponent + ".rels"
        guard let rels = try archive.contents(of: relsPath),
              let rel = RelationshipParser.parse(rels).first(where: { $0.type.hasSuffix("/notesSlide") }) else { return "" }
        let notesPath = PackagePath.resolve(rel.target, relativeTo: slidePath)
        guard let xml = try archive.contents(of: notesPath) else { return "" }
        // A notes page also holds a picture of the slide and its own number.
        return try ShapeTextParser.parse(xml, skipping: ["sldImg", "sldNum", "ftr", "dt", "hdr"])
            .flatMap(\.paragraphs)
            .joined(separator: "\n")
    }
}

/// Resolves relationship targets inside an Open Packaging Conventions package.
enum PackagePath {
    /// `../notesSlides/notesSlide1.xml` relative to `ppt/slides/slide1.xml`
    /// is `ppt/notesSlides/notesSlide1.xml`; targets starting with `/` are
    /// relative to the package root.
    static func resolve(_ target: String, relativeTo source: String) -> String {
        var parts: [Substring]
        if target.hasPrefix("/") {
            parts = []
        } else {
            parts = source.split(separator: "/")
            parts.removeLast()
        }
        for component in target.split(separator: "/") {
            switch component {
            case ".": continue
            case "..": if !parts.isEmpty { parts.removeLast() }
            default: parts.append(component)
            }
        }
        return parts.joined(separator: "/")
    }
}
