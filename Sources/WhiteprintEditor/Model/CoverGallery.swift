import AppKit

/// A banner from the built-in cover gallery: a public domain oil painting.
public struct Cover: Equatable {
    /// Stored in the note's `cover:` front matter field, and the image's file name.
    public let id: String
    public let title: String
    public let artist: String
    public let year: String

    /// "The Starry Night, Vincent van Gogh (1889)"
    public var caption: String { "\(title), \(artist) (\(year))" }
}

/// The built-in note covers, in `Resources/Covers` (see its CREDITS.md).
/// The app bundle carries them in `Contents/Resources/Covers`; dev builds
/// run outside a bundle read them from the source tree.
public enum CoverGallery {
    public static let covers: [Cover] = [
        Cover(id: "monet-water-lilies", title: "Water Lilies", artist: "Claude Monet", year: "1906"),
        Cover(id: "monet-impression-sunrise", title: "Impression, Sunrise", artist: "Claude Monet", year: "1872"),
        Cover(id: "van-gogh-starry-night", title: "The Starry Night", artist: "Vincent van Gogh", year: "1889"),
        Cover(id: "van-gogh-wheat-field-with-cypresses", title: "Wheat Field with Cypresses",
              artist: "Vincent van Gogh", year: "1889"),
        Cover(id: "van-gogh-almond-blossom", title: "Almond Blossom", artist: "Vincent van Gogh", year: "1890"),
        Cover(id: "turner-fighting-temeraire", title: "The Fighting Temeraire", artist: "J. M. W. Turner", year: "1839"),
        Cover(id: "vermeer-view-of-delft", title: "View of Delft", artist: "Johannes Vermeer", year: "c. 1660"),
        Cover(id: "seurat-grande-jatte", title: "A Sunday on La Grande Jatte", artist: "Georges Seurat", year: "1884–86"),
        Cover(id: "friedrich-wanderer", title: "Wanderer above the Sea of Fog",
              artist: "Caspar David Friedrich", year: "c. 1818"),
        Cover(id: "klimt-beech-grove", title: "Beech Grove I", artist: "Gustav Klimt", year: "1902"),
        Cover(id: "constable-hay-wain", title: "The Hay Wain", artist: "John Constable", year: "1821"),
        Cover(id: "bierstadt-sierra-nevada", title: "Among the Sierra Nevada", artist: "Albert Bierstadt", year: "1868"),
    ]

    public static func cover(id: String) -> Cover? {
        covers.first { $0.id == id }
    }

    /// A cover other than `current`, for the Random button.
    public static func random(excluding current: String?) -> Cover {
        covers.filter { $0.id != current }.randomElement() ?? covers[0]
    }

    /// Where the gallery's images are, or nil if they can't be found.
    static let folder: URL? = {
        let fileManager = FileManager.default
        if let bundled = Bundle.main.resourceURL?.appendingPathComponent("Covers"),
           fileManager.fileExists(atPath: bundled.path) {
            return bundled
        }
        // Sources/WhiteprintEditor/Model/CoverGallery.swift → the repository root.
        let source = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
            .appendingPathComponent("Resources/Covers")
        return fileManager.fileExists(atPath: source.path) ? source : nil
    }()

    static func imageURL(id: String) -> URL? {
        guard cover(id: id) != nil, let url = folder?.appendingPathComponent(id + ".jpg"),
              FileManager.default.fileExists(atPath: url.path) else { return nil }
        return url
    }

    private static let cache = NSCache<NSString, NSImage>()

    /// The cover's image; nil for an id that isn't in the gallery.
    public static func image(id: String) -> NSImage? {
        if let image = cache.object(forKey: id as NSString) { return image }
        guard let url = imageURL(id: id), let image = NSImage(contentsOf: url) else { return nil }
        cache.setObject(image, forKey: id as NSString)
        return image
    }
}
