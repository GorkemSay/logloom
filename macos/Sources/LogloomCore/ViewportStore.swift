import Foundation

public enum ViewportMode: Equatable, Sendable {
    case fullFile
    case filtered
}

/// Manages rows displayed by `VirtualizedLogTableView` across full-file paged access
/// and streaming filtered/trace-isolated query results.
public final class ViewportStore {
    public let pageCache: LinePageCache
    public private(set) var mode: ViewportMode = .fullFile

    private var filteredLines: [LogLine] = []

    public init(pageCache: LinePageCache = LinePageCache()) {
        self.pageCache = pageCache
    }

    public func configureFile(_ file: LogFile?, headLines: [LogLine]) {
        mode = .fullFile
        filteredLines.removeAll(keepingCapacity: true)
        pageCache.configure(file: file, headLines: headLines)
    }

    public func setFullFileMode() {
        mode = .fullFile
        filteredLines.removeAll(keepingCapacity: true)
    }

    public func beginFilteredMode() {
        mode = .filtered
        filteredLines.removeAll(keepingCapacity: true)
    }

    public func appendFilteredMatches(_ matches: [SearchMatch]) {
        guard !matches.isEmpty else { return }
        filteredLines.reserveCapacity(filteredLines.count + matches.count)
        for match in matches {
            filteredLines.append(
                LogLine(
                    lineNumber: match.lineNumber,
                    byteOffset: match.byteOffset,
                    byteLength: match.byteLength,
                    content: match.content,
                    hasCRLF: match.hasCRLF,
                    isTruncatedEOF: match.isTruncatedEOF,
                    exceedsMaxLength: false,
                    fields: match.fields
                )
            )
        }
    }

    public var rowCount: Int {
        switch mode {
        case .fullFile:
            return pageCache.rowCount
        case .filtered:
            return filteredLines.count
        }
    }

    public func line(atRow row: Int) -> LogLine? {
        guard row >= 0 else { return nil }
        switch mode {
        case .fullFile:
            return pageCache.line(atRow: row)
        case .filtered:
            guard row < filteredLines.count else { return nil }
            return filteredLines[row]
        }
    }

    public func rowIndex(forLineNumber lineNumber: UInt64) -> Int? {
        guard lineNumber > 0 else { return nil }
        switch mode {
        case .fullFile:
            let idx = Int(clamping: lineNumber - 1)
            return idx < pageCache.rowCount ? idx : nil

        case .filtered:
            var low = 0
            var high = filteredLines.count - 1
            while low <= high {
                let mid = low + (high - low) / 2
                let midLine = filteredLines[mid].lineNumber
                if midLine == lineNumber {
                    return mid
                } else if midLine < lineNumber {
                    low = mid + 1
                } else {
                    high = mid - 1
                }
            }
            return nil
        }
    }
}
