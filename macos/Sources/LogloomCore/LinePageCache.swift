import Foundation

/// Bounded LRU page cache over a `LogFile`'s pre-index head lines and indexed pages.
/// Thread safety: intended to be accessed from the UI/main actor only.
public final class LinePageCache {
    public let pageSize: Int
    public let maxCachedPages: Int

    private var logFile: LogFile?
    private var headLines: [LogLine] = []
    private var pages: [Int: [LogLine]] = [:]
    private var lruOrder: [Int] = []

    public init(pageSize: Int = 256, maxCachedPages: Int = 32) {
        self.pageSize = max(1, pageSize)
        self.maxCachedPages = max(1, maxCachedPages)
    }

    public func configure(file: LogFile?, headLines: [LogLine]) {
        self.logFile = file
        self.headLines = headLines
        clearCache()
    }

    public var rowCount: Int {
        if let file = logFile, file.isIndexed {
            return Int(clamping: file.lineCount)
        }
        return headLines.count
    }

    public var cachedPageCount: Int {
        pages.count
    }

    public func clearCache() {
        pages.removeAll(keepingCapacity: true)
        lruOrder.removeAll(keepingCapacity: true)
    }

    public func line(atRow row: Int) -> LogLine? {
        guard row >= 0 else { return nil }

        guard let file = logFile, file.isIndexed else {
            guard row < headLines.count else { return nil }
            return headLines[row]
        }

        let totalRows = Int(clamping: file.lineCount)
        guard row < totalRows else { return nil }

        let pageIndex = row / pageSize
        let offsetInPage = row % pageSize

        if let cachedPage = pages[pageIndex] {
            touchLRU(pageIndex: pageIndex)
            return offsetInPage < cachedPage.count ? cachedPage[offsetInPage] : nil
        }

        let startLine = UInt64(pageIndex * pageSize) + 1
        let remaining = totalRows - (pageIndex * pageSize)
        let fetchCount = min(pageSize, remaining)
        guard fetchCount > 0 else { return nil }

        do {
            let fetched = try file.readContext(
                around: startLine,
                before: 0,
                after: UInt64(fetchCount - 1)
            )
            insertPage(pageIndex: pageIndex, lines: fetched)
            return offsetInPage < fetched.count ? fetched[offsetInPage] : nil
        } catch {
            return nil
        }
    }

    private func touchLRU(pageIndex: Int) {
        if lruOrder.last == pageIndex {
            return
        }
        if let existingIdx = lruOrder.firstIndex(of: pageIndex) {
            lruOrder.remove(at: existingIdx)
        }
        lruOrder.append(pageIndex)
    }

    private func insertPage(pageIndex: Int, lines: [LogLine]) {
        pages[pageIndex] = lines
        touchLRU(pageIndex: pageIndex)

        while pages.count > maxCachedPages, let evicted = lruOrder.first {
            lruOrder.removeFirst()
            pages.removeValue(forKey: evicted)
        }
    }
}
