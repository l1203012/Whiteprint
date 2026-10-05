import Foundation

private enum Namespace {
    static let drawing = "http://schemas.openxmlformats.org/drawingml/2006/main"
    static let presentation = "http://schemas.openxmlformats.org/presentationml/2006/main"
    static let compatibility = "http://schemas.openxmlformats.org/markup-compatibility/2006"
}

struct XMLParseFailure: Error {}

/// Runs `delegate` over `data` with namespace processing on, so element names
/// arrive without their (arbitrary) prefixes.
private func run(_ delegate: XMLParserDelegate, on data: Data) throws {
    let parser = XMLParser(data: data)
    parser.shouldProcessNamespaces = true
    parser.shouldResolveExternalEntities = false
    parser.delegate = delegate
    guard parser.parse() else { throw XMLParseFailure() }
}

/// The text of one shape on a slide or notes page.
struct ShapeText: Equatable {
    /// The placeholder type (`title`, `body`, `sldNum`, …), or nil for a free shape.
    var placeholder: String?
    /// One entry per non-empty `<a:p>`.
    var paragraphs: [String]
}

/// Collects `<a:t>` runs per `<a:p>` paragraph, grouped by shape.
final class ShapeTextParser: NSObject, XMLParserDelegate {
    private let skipped: Set<String>
    private var shapes: [ShapeText] = []
    private var stack: [ShapeText] = []
    private var paragraph: String?
    private var inText = false
    private var fallbackDepth = 0

    private init(skipping skipped: Set<String>) {
        self.skipped = skipped
    }

    /// Shapes in document order, leaving out placeholders whose type is in `skipped`.
    static func parse(_ data: Data, skipping skipped: Set<String> = []) throws -> [ShapeText] {
        let parser = ShapeTextParser(skipping: skipped)
        try run(parser, on: data)
        return parser.shapes
    }

    private static let containers: Set<String> = ["sp", "graphicFrame"]

    func parser(_ parser: XMLParser, didStartElement name: String, namespaceURI: String?, qualifiedName: String?, attributes: [String: String] = [:]) {
        if fallbackDepth > 0 || (namespaceURI == Namespace.compatibility && name == "Fallback") {
            fallbackDepth += 1
            return
        }
        switch (namespaceURI, name) {
        case (Namespace.presentation, let name) where Self.containers.contains(name):
            stack.append(ShapeText(placeholder: nil, paragraphs: []))
        case (Namespace.presentation, "ph"):
            if !stack.isEmpty { stack[stack.count - 1].placeholder = attributes["type"] ?? "body" }
        case (Namespace.drawing, "p"):
            paragraph = ""
        case (Namespace.drawing, "t"):
            inText = true
        case (Namespace.drawing, "br"):
            paragraph? += "\n"
        case (Namespace.drawing, "tab"):
            paragraph? += " "
        default:
            break
        }
    }

    func parser(_ parser: XMLParser, didEndElement name: String, namespaceURI: String?, qualifiedName: String?) {
        if fallbackDepth > 0 {
            fallbackDepth -= 1
            return
        }
        switch (namespaceURI, name) {
        case (Namespace.presentation, let name) where Self.containers.contains(name):
            guard let shape = stack.popLast() else { return }
            if shape.placeholder.map(skipped.contains) != true && !shape.paragraphs.isEmpty {
                shapes.append(shape)
            }
        case (Namespace.drawing, "p"):
            let text = paragraph?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            paragraph = nil
            guard !text.isEmpty else { return }
            if stack.isEmpty {
                shapes.append(ShapeText(placeholder: nil, paragraphs: [text]))
            } else {
                stack[stack.count - 1].paragraphs.append(text)
            }
        case (Namespace.drawing, "t"):
            inText = false
        default:
            break
        }
    }

    func parser(_ parser: XMLParser, foundCharacters string: String) {
        if inText && fallbackDepth == 0 { paragraph? += string }
    }
}

/// One `<Relationship>` from a `.rels` part.
struct Relationship: Equatable {
    var id: String
    var type: String
    var target: String
}

final class RelationshipParser: NSObject, XMLParserDelegate {
    private var relationships: [Relationship] = []

    /// Internal relationships; external links are left out. Malformed parts
    /// yield what was read before the error.
    static func parse(_ data: Data) -> [Relationship] {
        let parser = RelationshipParser()
        try? run(parser, on: data)
        return parser.relationships
    }

    func parser(_ parser: XMLParser, didStartElement name: String, namespaceURI: String?, qualifiedName: String?, attributes: [String: String] = [:]) {
        guard name == "Relationship", attributes["TargetMode"] != "External",
              let id = attributes["Id"], let type = attributes["Type"], let target = attributes["Target"] else { return }
        relationships.append(Relationship(id: id, type: type, target: target))
    }
}

/// Reads the relationship ids of `<p:sldId>` entries, in presentation order.
final class SlideListParser: NSObject, XMLParserDelegate {
    private var ids: [String] = []

    static func parse(_ data: Data) -> [String] {
        let parser = SlideListParser()
        try? run(parser, on: data)
        return parser.ids
    }

    func parser(_ parser: XMLParser, didStartElement name: String, namespaceURI: String?, qualifiedName: String?, attributes: [String: String] = [:]) {
        guard namespaceURI == Namespace.presentation, name == "sldId" else { return }
        // The relationship id is the namespaced `r:id`, not the plain numeric `id`.
        if let id = attributes.first(where: { $0.key.hasSuffix(":id") })?.value {
            ids.append(id)
        }
    }
}
