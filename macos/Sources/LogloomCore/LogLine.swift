import CLogloom
import Foundation

public enum LogLineKind: Sendable, Equatable {
    case plainText
    case jsonObject
    case malformed

    init(cKind: logloom_line_kind_t) {
        switch cKind {
        case LOGLOOM_LINE_JSON_OBJECT:
            self = .jsonObject
        case LOGLOOM_LINE_MALFORMED:
            self = .malformed
        default:
            self = .plainText
        }
    }
}

public struct ExtractedFields: Sendable, Equatable {
    public let kind: LogLineKind
    public let timestamp: String?
    public let level: String?
    public let traceID: String?
    public let hasEscapesInTraceID: Bool

    public init(
        kind: LogLineKind,
        timestamp: String? = nil,
        level: String? = nil,
        traceID: String? = nil,
        hasEscapesInTraceID: Bool = false
    ) {
        self.kind = kind
        self.timestamp = timestamp
        self.level = level
        self.traceID = traceID
        self.hasEscapesInTraceID = hasEscapesInTraceID
    }
}

public struct LogLine: Identifiable, Sendable, Equatable {
    public var id: UInt64 { lineNumber }

    public let lineNumber: UInt64
    public let byteOffset: UInt64
    public let byteLength: UInt64
    public let content: String
    public let hasCRLF: Bool
    public let isTruncatedEOF: Bool
    public let exceedsMaxLength: Bool
    public let fields: ExtractedFields
}

public struct IndexStats: Sendable, Equatable {
    public let totalLines: UInt64
    public let oversizedLines: UInt64
    public let bytesRead: UInt64
    public let stride: UInt64
    public let indexMemoryBytes: Int
    public let hadUTF8BOM: Bool
    public let hasTruncatedFinalLine: Bool
}

public struct SearchQuery: Sendable, Equatable {
    public var traceID: String?
    public var traceKey: String
    public var level: String?
    public var levelKey: String
    public var timestampKey: String
    public var textQuery: String?
    public var caseSensitive: Bool
    public var isRegex: Bool
    public var limit: UInt64
    public var numThreads: Int

    public init(
        traceID: String? = nil,
        traceKey: String = "request_id",
        level: String? = nil,
        levelKey: String = "level",
        timestampKey: String = "timestamp",
        textQuery: String? = nil,
        caseSensitive: Bool = true,
        isRegex: Bool = false,
        limit: UInt64 = 0,
        numThreads: Int = 0
    ) {
        self.traceID = traceID
        self.traceKey = traceKey
        self.level = level
        self.levelKey = levelKey
        self.timestampKey = timestampKey
        self.textQuery = textQuery
        self.caseSensitive = caseSensitive
        self.isRegex = isRegex
        self.limit = limit
        self.numThreads = numThreads
    }
}

public struct SearchMatch: Identifiable, Sendable, Equatable {
    public var id: UInt64 { lineNumber }

    public let lineNumber: UInt64
    public let byteOffset: UInt64
    public let byteLength: UInt64
    public let content: String
    public let fields: ExtractedFields
    public let hasCRLF: Bool
    public let isTruncatedEOF: Bool
}

public struct SearchStats: Sendable, Equatable {
    public let linesScanned: UInt64
    public let matchesFound: UInt64
    public let jsonLines: UInt64
    public let plainTextLines: UInt64
    public let malformedLines: UInt64
    public let oversizedLines: UInt64
    public let truncatedLines: UInt64
    public let hasTruncatedFinalLine: Bool
    public let cancelled: Bool
    public let limitReached: Bool
}
