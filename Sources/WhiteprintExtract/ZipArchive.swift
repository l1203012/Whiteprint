import Compression
import Foundation

/// A minimal read-only ZIP reader: enough for Office Open XML packages.
///
/// The archive is memory-mapped and only the central directory is parsed up
/// front; entries are inflated one at a time on request. Supports stored and
/// deflated entries, not ZIP64, encryption or multi-disk archives.
struct ZipArchive {
    enum Failure: Error, Equatable {
        case notAZip
        case corrupt(String)
        case unsupported(String)
        case tooLarge(String)
    }

    struct Entry: Equatable {
        let name: String
        let method: UInt16
        let flags: UInt16
        let crc32: UInt32
        let compressedSize: Int
        let uncompressedSize: Int
        let localHeaderOffset: Int
    }

    /// Entries larger than this when inflated are refused (zip bombs, corrupt sizes).
    static let maxEntrySize = 64 << 20

    private let data: Data
    /// Entries by name, in central directory order.
    let entries: [Entry]
    private let index: [String: Int]

    init(url: URL) throws {
        try self.init(data: Data(contentsOf: url, options: .alwaysMapped))
    }

    init(data: Data) throws {
        self.data = data
        let bytes = ByteReader(data)
        let eocd = try Self.findEndOfCentralDirectory(bytes)
        let count = try bytes.u16(eocd + 10)
        let size = try Int(bytes.u32(eocd + 12))
        let offset = try Int(bytes.u32(eocd + 16))
        if count == 0xFFFF || size == 0xFFFF_FFFF || offset == 0xFFFF_FFFF {
            throw Failure.unsupported("ZIP64")
        }
        guard offset + size <= eocd else { throw Failure.corrupt("central directory out of range") }

        var entries: [Entry] = []
        var index: [String: Int] = [:]
        var position = offset
        for _ in 0..<count {
            guard try bytes.u32(position) == 0x0201_4B50 else { throw Failure.corrupt("bad central directory entry") }
            let nameLength = try Int(bytes.u16(position + 28))
            let extraLength = try Int(bytes.u16(position + 30))
            let commentLength = try Int(bytes.u16(position + 32))
            let entry = try Entry(
                name: bytes.string(position + 46, length: nameLength),
                method: bytes.u16(position + 10),
                flags: bytes.u16(position + 8),
                crc32: bytes.u32(position + 16),
                compressedSize: Int(bytes.u32(position + 20)),
                uncompressedSize: Int(bytes.u32(position + 24)),
                localHeaderOffset: Int(bytes.u32(position + 42))
            )
            if index[entry.name] == nil { index[entry.name] = entries.count }
            entries.append(entry)
            position += 46 + nameLength + extraLength + commentLength
        }
        self.entries = entries
        self.index = index
    }

    func entry(named name: String) -> Entry? {
        index[name].map { entries[$0] }
    }

    /// Inflates the named entry, or returns nil if the archive has no such entry.
    func contents(of name: String) throws -> Data? {
        guard let entry = entry(named: name) else { return nil }
        return try contents(of: entry)
    }

    func contents(of entry: Entry) throws -> Data {
        if entry.flags & 1 != 0 { throw Failure.unsupported("encrypted entry \(entry.name)") }
        guard entry.uncompressedSize <= Self.maxEntrySize else { throw Failure.tooLarge(entry.name) }
        let bytes = ByteReader(data)
        let header = entry.localHeaderOffset
        guard try bytes.u32(header) == 0x0403_4B50 else { throw Failure.corrupt("bad local header for \(entry.name)") }
        let start = try header + 30 + Int(bytes.u16(header + 26)) + Int(bytes.u16(header + 28))
        guard start + entry.compressedSize <= data.count else { throw Failure.corrupt("\(entry.name) out of range") }
        let compressed = data.subdata(in: data.startIndex + start ..< data.startIndex + start + entry.compressedSize)

        let output: Data
        switch entry.method {
        case 0:
            guard entry.compressedSize == entry.uncompressedSize else { throw Failure.corrupt("stored size mismatch for \(entry.name)") }
            output = compressed
        case 8:
            output = try Self.inflate(compressed, size: entry.uncompressedSize, name: entry.name)
        default:
            throw Failure.unsupported("compression method \(entry.method) for \(entry.name)")
        }
        guard CRC32.checksum(output) == entry.crc32 else { throw Failure.corrupt("checksum mismatch for \(entry.name)") }
        return output
    }

    /// Decodes raw DEFLATE data (`COMPRESSION_ZLIB` is headerless DEFLATE).
    private static func inflate(_ compressed: Data, size: Int, name: String) throws -> Data {
        if size == 0 { return Data() }
        // One spare byte tells a correct size apart from a truncated output.
        var output = Data(count: size + 1)
        let written = output.withUnsafeMutableBytes { (dst: UnsafeMutableRawBufferPointer) -> Int in
            compressed.withUnsafeBytes { (src: UnsafeRawBufferPointer) -> Int in
                guard let dstBase = dst.bindMemory(to: UInt8.self).baseAddress,
                      let srcBase = src.bindMemory(to: UInt8.self).baseAddress else { return 0 }
                return compression_decode_buffer(dstBase, size + 1, srcBase, compressed.count, nil, COMPRESSION_ZLIB)
            }
        }
        guard written == size else { throw Failure.corrupt("can't inflate \(name)") }
        output.count = size
        return output
    }

    private static func findEndOfCentralDirectory(_ bytes: ByteReader) throws -> Int {
        let count = bytes.data.count
        guard count >= 22 else { throw Failure.notAZip }
        // The record is 22 bytes plus a comment of at most 65535 bytes.
        let lowest = max(0, count - 22 - 0xFFFF)
        var position = count - 22
        while position >= lowest {
            if try bytes.u32(position) == 0x0605_4B50 { return position }
            position -= 1
        }
        throw Failure.notAZip
    }
}

/// Bounds-checked little-endian reads at absolute offsets.
private struct ByteReader {
    let data: Data

    init(_ data: Data) {
        self.data = data
    }

    func u16(_ offset: Int) throws -> UInt16 {
        let bytes = try range(offset, 2)
        return UInt16(bytes[bytes.startIndex]) | UInt16(bytes[bytes.startIndex + 1]) << 8
    }

    func u32(_ offset: Int) throws -> UInt32 {
        let bytes = try range(offset, 4)
        return (0..<4).reduce(UInt32(0)) { $0 | UInt32(bytes[bytes.startIndex + $1]) << (8 * UInt32($1)) }
    }

    func string(_ offset: Int, length: Int) throws -> String {
        let bytes = try range(offset, length)
        return String(decoding: bytes, as: UTF8.self)
    }

    private func range(_ offset: Int, _ length: Int) throws -> Data.SubSequence {
        guard offset >= 0, length >= 0, offset + length <= data.count else {
            throw ZipArchive.Failure.corrupt("read past end of archive")
        }
        return data[data.startIndex + offset ..< data.startIndex + offset + length]
    }
}

enum CRC32 {
    private static let table: [UInt32] = (0..<256).map { n in
        (0..<8).reduce(UInt32(n)) { c, _ in c & 1 != 0 ? 0xEDB8_8320 ^ (c >> 1) : c >> 1 }
    }

    static func checksum(_ data: Data) -> UInt32 {
        var crc: UInt32 = 0xFFFF_FFFF
        data.withUnsafeBytes { (buffer: UnsafeRawBufferPointer) in
            for byte in buffer {
                crc = table[Int((crc ^ UInt32(byte)) & 0xFF)] ^ (crc >> 8)
            }
        }
        return crc ^ 0xFFFF_FFFF
    }
}
