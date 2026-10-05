import Foundation

/// One page, slide or section of a source document.
public struct ExtractedUnit: Codable, Equatable {
    /// Human-readable location, e.g. `p. 6`, `slide 14`, `section 3`.
    public var ref: String
    public var text: String

    public init(ref: String, text: String) {
        self.ref = ref
        self.text = text
    }
}

public struct ExtractedDocument: Codable, Equatable {
    /// File name, e.g. `Lecture3.pptx`.
    public var name: String
    /// Cleaned units in document order. Empty units are dropped.
    public var units: [ExtractedUnit]

    public init(name: String, units: [ExtractedUnit]) {
        self.name = name
        self.units = units
    }
}

/// A slice of a document small enough to send to Claude in one tool result.
public struct ExtractedChunk: Codable, Equatable {
    /// 1-based.
    public var index: Int
    public var firstRef: String
    public var lastRef: String
    /// Units joined with `[ref]` marker lines, e.g. `[slide 14]`, so every line
    /// can be traced back to its source.
    public var text: String

    public init(index: Int, firstRef: String, lastRef: String, text: String) {
        self.index = index
        self.firstRef = firstRef
        self.lastRef = lastRef
        self.text = text
    }
}

public enum ExtractionError: Error, Equatable, CustomStringConvertible {
    case unsupportedType(String)
    case unreadable(String)
    /// The file has no extractable text, even after OCR.
    case empty(String)

    public var description: String {
        switch self {
        case .unsupportedType(let name): return "\(name): unsupported file type (use PDF, DOCX, DOC or PPTX)"
        case .unreadable(let name): return "\(name): can't be read"
        case .empty(let name): return "\(name): no text found"
        }
    }
}

public enum DocumentExtractor {
    public static let supportedExtensions: Set<String> = ["pdf", "docx", "doc", "pptx"]

    /// Extracts and cleans text locally. Runs synchronously and may take a
    /// while for large or scanned files, so call it off the main thread.
    /// Memory stays flat: one page or slide is processed at a time.
    public static func extract(_ url: URL, ocr: Bool = true) throws -> ExtractedDocument {
        let name = url.lastPathComponent
        let type = url.pathExtension.lowercased()
        guard supportedExtensions.contains(type) else { throw ExtractionError.unsupportedType(name) }
        guard FileManager.default.isReadableFile(atPath: url.path) else { throw ExtractionError.unreadable(name) }

        let raw: [ExtractedUnit]
        switch type {
        case "pdf": raw = try PDFExtractor.extract(url, ocr: ocr)
        case "docx": raw = try RichTextExtractor.extract(url, type: .officeOpenXML)
        case "doc": raw = try RichTextExtractor.extract(url, type: .docFormat)
        default: raw = try PresentationExtractor.extract(url)
        }
        let units = TextCleaner.clean(raw)
        guard !units.isEmpty else { throw ExtractionError.empty(name) }
        return ExtractedDocument(name: name, units: units)
    }
}

public enum Chunker {
    /// About 6k tokens.
    public static let defaultMaxCharacters = 24_000

    /// Splits at unit boundaries; a single oversized unit is split at paragraph
    /// or line boundaries.
    public static func chunks(_ document: ExtractedDocument, maxCharacters: Int = defaultMaxCharacters) -> [ExtractedChunk] {
        makeChunks(document, maxCharacters: maxCharacters)
    }
}
