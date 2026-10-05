/// Shared types for the study agent. Claude sends these through the MCP
/// study tools; WhiteprintStudy stores them and renders the final note.

public enum Importance: String, Codable, Equatable, CaseIterable {
    /// ★ Core concepts, likely exam material.
    case must
    /// ○ Supporting detail.
    case good
    /// ✕ Examples, history, repetition, filler.
    case skip
}

/// One finding from a chunk of source material.
public struct StudyPoint: Codable, Equatable {
    public var text: String
    public var importance: Importance
    /// Where it came from, e.g. `Lecture3.pptx · slide 14`.
    public var ref: String
    /// Optional topic hint used when grouping points into modules.
    public var topic: String?

    public init(text: String, importance: Importance, ref: String, topic: String? = nil) {
        self.text = text
        self.importance = importance
        self.ref = ref
        self.topic = topic
    }
}

/// Something to do: an assignment, exercise, reading or deadline.
public struct StudyTask: Codable, Equatable {
    public var text: String
    /// Free-form due date as found in the material, e.g. `2026-01-15` or `week 6`.
    public var due: String?
    public var ref: String?

    public init(text: String, due: String? = nil, ref: String? = nil) {
        self.text = text
        self.due = due
        self.ref = ref
    }
}

public struct StudyModule: Codable, Equatable {
    public var title: String
    /// Estimated study time.
    public var minutes: Int?
    public var points: [StudyPoint]

    public init(title: String, minutes: Int? = nil, points: [StudyPoint]) {
        self.title = title
        self.minutes = minutes
        self.points = points
    }
}

/// The finished plan Claude sends to `build_study_plan`.
public struct StudyPlan: Codable, Equatable {
    public var title: String
    /// 5–10 lines on what the material covers.
    public var overview: String
    /// In the order they should be studied.
    public var modules: [StudyModule]
    public var tasks: [StudyTask]
    /// Optional diagrams in the drawing language.
    public var diagrams: [String]

    public init(title: String, overview: String, modules: [StudyModule], tasks: [StudyTask] = [], diagrams: [String] = []) {
        self.title = title
        self.overview = overview
        self.modules = modules
        self.tasks = tasks
        self.diagrams = diagrams
    }
}
