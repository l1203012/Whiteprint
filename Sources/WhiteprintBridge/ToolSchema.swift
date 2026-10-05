import Foundation

/// The small subset of JSON Schema the tools use. One description produces both
/// the `inputSchema` sent to Claude and the validation of incoming arguments,
/// so the two can't drift apart.
indirect enum ToolSchema {
    case string
    case integer
    case oneOf([String])
    case array(ToolSchema)
    case object([Property])

    struct Property {
        let name: String
        let schema: ToolSchema
        let required: Bool

        static func required(_ name: String, _ schema: ToolSchema) -> Property {
            Property(name: name, schema: schema, required: true)
        }

        static func optional(_ name: String, _ schema: ToolSchema) -> Property {
            Property(name: name, schema: schema, required: false)
        }
    }

    var json: [String: Any] {
        switch self {
        case .string:
            return ["type": "string"]
        case .integer:
            return ["type": "integer"]
        case let .oneOf(values):
            return ["type": "string", "enum": values]
        case let .array(items):
            return ["type": "array", "items": items.json]
        case let .object(properties):
            var json: [String: Any] = ["type": "object"]
            json["properties"] = Dictionary(uniqueKeysWithValues: properties.map { ($0.name, $0.schema.json) })
            let required = properties.filter(\.required).map(\.name)
            if !required.isEmpty {
                json["required"] = required
            }
            return json
        }
    }

    /// Checks `value` and returns it normalised: integral numbers and numeric
    /// strings become `Int`. Errors name the offending path, e.g. `points[2].importance`.
    func validate(_ value: Any, at path: String = "") throws -> Any {
        switch self {
        case .string:
            guard let string = value as? String else { throw ArgumentError(path, "expected a string") }
            return string
        case .integer:
            if let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID(),
               let int = Int(exactly: number.doubleValue) {
                return int
            }
            if let string = value as? String, let int = Int(string.trimmingCharacters(in: .whitespaces)) {
                return int
            }
            throw ArgumentError(path, "expected an integer")
        case let .oneOf(values):
            guard let string = value as? String, values.contains(string) else {
                throw ArgumentError(path, "expected one of \(values.joined(separator: ", "))")
            }
            return string
        case let .array(items):
            guard let array = value as? [Any] else { throw ArgumentError(path, "expected an array") }
            return try array.enumerated().map { try items.validate($0.element, at: "\(path)[\($0.offset)]") }
        case let .object(properties):
            guard let object = value as? [String: Any] else { throw ArgumentError(path, "expected an object") }
            let prefix = path.isEmpty ? "" : path + "."
            let names = properties.map(\.name)
            if let unknown = object.keys.sorted().first(where: { !names.contains($0) }) {
                throw ArgumentError(prefix + unknown, "unknown argument (expected \(names.joined(separator: ", ")))")
            }
            var result: [String: Any] = [:]
            for property in properties {
                guard let field = object[property.name], !(field is NSNull) else {
                    if property.required { throw ArgumentError(prefix + property.name, "required") }
                    continue
                }
                result[property.name] = try property.schema.validate(field, at: prefix + property.name)
            }
            return result
        }
    }
}

/// A bad tool argument, reported to Claude as `path: problem`.
struct ArgumentError: Error, CustomStringConvertible {
    let path: String
    let problem: String

    init(_ path: String, _ problem: String) {
        self.path = path
        self.problem = problem
    }

    var description: String {
        path.isEmpty ? problem : "\(path): \(problem)"
    }
}
