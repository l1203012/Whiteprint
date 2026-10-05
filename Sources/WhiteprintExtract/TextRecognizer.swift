import CoreGraphics
import Foundation
import Vision

/// On-device OCR with Vision.
enum TextRecognizer {
    /// Languages we ask for when Vision supports them, in priority order.
    static let preferredLanguages = ["en-US", "nl-NL", "fr-FR", "de-DE", "es-ES", "it-IT", "pt-BR"]

    /// The preferred languages this Mac's accurate recognizer supports.
    static let languages: [String] = {
        let request = VNRecognizeTextRequest()
        request.recognitionLevel = .accurate
        let supported = Set((try? request.supportedRecognitionLanguages()) ?? ["en-US"])
        return preferredLanguages.filter { supported.contains($0) }
    }()

    /// Returns the recognized lines top to bottom, or an empty string.
    static func recognize(_ image: CGImage) -> String {
        let request = VNRecognizeTextRequest()
        request.recognitionLevel = .accurate
        request.usesLanguageCorrection = true
        request.recognitionLanguages = languages
        let handler = VNImageRequestHandler(cgImage: image, options: [:])
        guard (try? handler.perform([request])) != nil, let observations = request.results else { return "" }
        return observations
            // Vision's coordinates start at the bottom left.
            .sorted { ($0.boundingBox.maxY, -$0.boundingBox.minX) > ($1.boundingBox.maxY, -$1.boundingBox.minX) }
            .compactMap { $0.topCandidates(1).first?.string }
            .joined(separator: "\n")
    }
}
