import AppKit
import CoreText
@testable import WhiteprintExtract

/// Generates test documents on the fly, so no binary fixtures are committed.
enum Fixtures {
    static func temporaryDirectory() throws -> URL {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("WhiteprintExtractTests-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    // MARK: PDF

    enum PDFPage {
        /// Lines of real text, top to bottom; an empty line leaves a wide gap.
        case text([String])
        /// A picture of these lines, with no text layer.
        case image([String])
        case blank
    }

    static let pageSize = CGSize(width: 612, height: 792)

    static func writePDF(_ pages: [PDFPage], to url: URL) throws {
        var box = CGRect(origin: .zero, size: pageSize)
        guard let context = CGContext(url as CFURL, mediaBox: &box, nil) else { throw FixtureError("can't create PDF") }
        for page in pages {
            context.beginPDFPage(nil)
            switch page {
            case .text(let lines):
                draw(lines, in: context, fontSize: 12, scale: 1)
            case .image(let lines):
                guard let image = picture(of: lines) else { throw FixtureError("can't draw image") }
                context.draw(image, in: box)
            case .blank:
                break
            }
            context.endPDFPage()
        }
        context.closePDF()
    }

    private static func draw(_ lines: [String], in context: CGContext, fontSize: CGFloat, scale: CGFloat) {
        let font = CTFontCreateWithName("Helvetica" as CFString, fontSize * scale, nil)
        var y = (pageSize.height - 50) * scale
        for line in lines {
            if line.isEmpty {
                y -= fontSize * 8 * scale
                continue
            }
            let attributed = NSAttributedString(string: line, attributes: [
                NSAttributedString.Key(kCTFontAttributeName as String): font,
                NSAttributedString.Key(kCTForegroundColorAttributeName as String): CGColor(gray: 0, alpha: 1),
            ])
            context.textPosition = CGPoint(x: 50 * scale, y: y)
            CTLineDraw(CTLineCreateWithAttributedString(attributed), context)
            y -= fontSize * 1.6 * scale
        }
    }

    private static func picture(of lines: [String]) -> CGImage? {
        let scale: CGFloat = 3
        guard let context = CGContext(
            data: nil, width: Int(pageSize.width * scale), height: Int(pageSize.height * scale),
            bitsPerComponent: 8, bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue
        ) else { return nil }
        context.setFillColor(gray: 1, alpha: 1)
        context.fill(CGRect(x: 0, y: 0, width: context.width, height: context.height))
        draw(lines, in: context, fontSize: 22, scale: scale)
        return context.makeImage()
    }

    // MARK: Word

    static func heading(_ text: String) -> NSAttributedString {
        NSAttributedString(string: text + "\n", attributes: [.font: NSFont.boldSystemFont(ofSize: 18)])
    }

    static func body(_ text: String) -> NSAttributedString {
        NSAttributedString(string: text + "\n", attributes: [.font: NSFont.systemFont(ofSize: 12)])
    }

    static func writeWord(_ parts: [NSAttributedString], type: NSAttributedString.DocumentType, to url: URL) throws {
        let string = NSMutableAttributedString()
        parts.forEach(string.append)
        let data = try string.data(from: NSRange(location: 0, length: string.length), documentAttributes: [.documentType: type])
        try data.write(to: url)
    }

    // MARK: ZIP / PPTX

    /// Zips `files` (path → contents) with `/usr/bin/zip`. Paths in `stored`
    /// are added uncompressed.
    static func writeZip(_ files: [String: String], stored: Set<String> = [], to url: URL) throws {
        let root = try temporaryDirectory()
        defer { try? FileManager.default.removeItem(at: root) }
        for (path, contents) in files {
            let file = root.appendingPathComponent(path)
            try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
            try Data(contents.utf8).write(to: file)
        }
        let deflated = files.keys.filter { !stored.contains($0) }.sorted()
        if !deflated.isEmpty { try zip(["-X", "-9", url.path] + deflated, in: root) }
        if !stored.isEmpty { try zip(["-X", "-0", url.path] + stored.sorted(), in: root) }
    }

    private static func zip(_ arguments: [String], in directory: URL) throws {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/zip")
        process.arguments = ["-q"] + arguments
        process.currentDirectoryURL = directory
        try process.run()
        process.waitUntilExit()
        guard process.terminationStatus == 0 else { throw FixtureError("zip failed") }
    }

    /// A one-entry stored ZIP built by hand, for corrupting headers on purpose.
    static func storedZip(name: String, contents: Data) -> Data {
        var data = Data()
        let crc = CRC32.checksum(contents)
        let nameBytes = Data(name.utf8)
        func u16(_ v: Int) { withUnsafeBytes(of: UInt16(v).littleEndian) { data.append(contentsOf: $0) } }
        func u32(_ v: UInt32) { withUnsafeBytes(of: v.littleEndian) { data.append(contentsOf: $0) } }

        u32(0x0403_4B50); u16(20); u16(0); u16(0); u16(0); u16(0)
        u32(crc); u32(UInt32(contents.count)); u32(UInt32(contents.count))
        u16(nameBytes.count); u16(0)
        data.append(nameBytes)
        data.append(contents)

        let directory = data.count
        u32(0x0201_4B50); u16(20); u16(20); u16(0); u16(0); u16(0); u16(0)
        u32(crc); u32(UInt32(contents.count)); u32(UInt32(contents.count))
        u16(nameBytes.count); u16(0); u16(0); u16(0); u16(0); u32(0); u32(0)
        data.append(nameBytes)
        let directorySize = data.count - directory

        u32(0x0605_4B50); u16(0); u16(0); u16(1); u16(1)
        u32(UInt32(directorySize)); u32(UInt32(directory)); u16(0)
        return data
    }

    static let drawingNS = "http://schemas.openxmlformats.org/drawingml/2006/main"
    static let presentationNS = "http://schemas.openxmlformats.org/presentationml/2006/main"
    static let relationshipsNS = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"

    /// A shape with an optional placeholder type and one `<a:p>` per paragraph;
    /// a paragraph's runs are separated by `|`.
    static func shape(_ placeholder: String?, _ paragraphs: [String]) -> String {
        let ph = placeholder.map { "<p:ph type=\"\($0)\"/>" } ?? ""
        let body = paragraphs.map { paragraph in
            "<a:p>" + paragraph.split(separator: "|", omittingEmptySubsequences: false)
                .map { "<a:r><a:rPr lang=\"en-US\"/><a:t>\($0)</a:t></a:r>" }.joined() + "</a:p>"
        }.joined()
        return "<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Shape\"/><p:cNvSpPr/><p:nvPr>\(ph)</p:nvPr></p:nvSpPr>"
            + "<p:spPr/><p:txBody><a:bodyPr/>\(body)</p:txBody></p:sp>"
    }

    static func slideXML(_ shapes: [String], root: String = "sld") -> String {
        """
        <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
        <p:\(root) xmlns:a="\(drawingNS)" xmlns:p="\(presentationNS)" xmlns:r="\(relationshipsNS)">\
        <p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>\
        <p:grpSpPr/>\(shapes.joined())</p:spTree></p:cSld></p:\(root)>
        """
    }

    static func notesRels(_ target: String) -> String {
        """
        <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
        <Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\
        <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout" Target="../slideLayouts/slideLayout1.xml"/>\
        <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/notesSlide" Target="\(target)"/>\
        </Relationships>
        """
    }

    /// `presentation.xml` and its rels listing `slides` (part names under `ppt/slides/`) in order.
    static func presentationParts(_ slides: [String]) -> [String: String] {
        let ids = slides.indices.map { "<p:sldId id=\"\(256 + $0)\" r:id=\"rId\($0 + 10)\"/>" }.joined()
        let rels = slides.enumerated().map {
            "<Relationship Id=\"rId\($0.offset + 10)\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide\" Target=\"slides/\($0.element)\"/>"
        }.joined()
        return [
            "ppt/presentation.xml": """
            <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
            <p:presentation xmlns:a="\(drawingNS)" xmlns:p="\(presentationNS)" xmlns:r="\(relationshipsNS)"><p:sldIdLst>\(ids)</p:sldIdLst></p:presentation>
            """,
            "ppt/_rels/presentation.xml.rels": """
            <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
            <Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\(rels)</Relationships>
            """,
        ]
    }

    static let contentTypes = """
    <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
    <Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="xml" ContentType="application/xml"/></Types>
    """
}

struct FixtureError: Error, CustomStringConvertible {
    let description: String

    init(_ description: String) {
        self.description = description
    }
}
