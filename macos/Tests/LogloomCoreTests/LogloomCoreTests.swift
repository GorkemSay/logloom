import Foundation
import LogloomCore
import XCTest

final class LogloomCoreTests: XCTestCase {
    private var tempFiles: [URL] = []

    override func tearDownWithError() throws {
        for url in tempFiles {
            try? FileManager.default.removeItem(at: url)
        }
        tempFiles.removeAll()
    }

    private func makeFixture(_ contents: String) throws -> URL {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("logloom_swift_test_\(UUID().uuidString).jsonl")
        try contents.write(to: url, atomically: true, encoding: .utf8)
        tempFiles.append(url)
        return url
    }

    func testEngineVersionAndErrorOnMissingFile() {
        XCTAssertEqual(LogFile.engineVersion, "0.1.0")
        XCTAssertThrowsError(try LogFile(path: "/tmp/logloom_swift_missing_file_999.jsonl")) { err in
            guard case LogloomError.io = err else {
                XCTFail("Expected LogloomError.io, got \(err)")
                return
            }
        }
    }

    func testReadHeadExtractFieldsIndexAndContext() throws {
        let payload = """
        {"timestamp":"2026-11-26T10:00:00Z","level":"INFO","request_id":"req-1","msg":"boot"}
        plain noise line
        {"timestamp":"2026-11-26T10:00:01Z","level":"ERROR","request_id":"req-2","msg":"timeout"}
        {"timestamp":"2026-11-26T10:00:02Z","level":"WARN","request_id":"req-1","msg":"retry"}
        {malformed
        """
        let url = try makeFixture(payload)
        let file = try LogFile(path: url.path)

        XCTAssertEqual(file.fileSize, UInt64(payload.utf8.count))
        XCTAssertFalse(file.isIndexed)

        let head = try file.readHead(maxLines: 2)
        XCTAssertEqual(head.count, 2)
        XCTAssertEqual(head[0].lineNumber, 1)
        XCTAssertEqual(head[0].fields.kind, .jsonObject)
        XCTAssertEqual(head[0].fields.level, "INFO")
        XCTAssertEqual(head[0].fields.traceID, "req-1")
        XCTAssertEqual(head[1].lineNumber, 2)
        XCTAssertEqual(head[1].fields.kind, .plainText)

        XCTAssertThrowsError(try file.readLine(1)) { err in
            guard case LogloomError.notIndexed = err else {
                XCTFail("Expected notIndexed, got \(err)")
                return
            }
        }

        var progressCalls = 0
        let stats = try file.buildIndex { processed, total, _ in
            progressCalls += 1
            XCTAssertLessThanOrEqual(processed, total)
            return true
        }
        XCTAssertTrue(file.isIndexed)
        XCTAssertEqual(file.lineCount, 5)
        XCTAssertEqual(stats.totalLines, 5)
        XCTAssertGreaterThanOrEqual(progressCalls, 1)

        let line3 = try file.readLine(3)
        XCTAssertEqual(line3.fields.level, "ERROR")
        XCTAssertEqual(line3.fields.traceID, "req-2")

        let ctx = try file.readContext(around: 3, before: 1, after: 1)
        XCTAssertEqual(ctx.map(\.lineNumber), [2, 3, 4])
    }

    func testSearchTraceRegexAndCancellation() throws {
        let payload = """
        {"timestamp":"t1","level":"INFO","request_id":"req-1","msg":"ok"}
        {"timestamp":"t2","level":"ERROR","request_id":"req-2","msg":"timeout"}
        {"timestamp":"t3","level":"WARN","request_id":"req-1","msg":"retry"}
        """
        let url = try makeFixture(payload)
        let file = try LogFile(path: url.path)

        var matches: [SearchMatch] = []
        let traceStats = try file.search(SearchQuery(traceID: "req-1")) { match in
            matches.append(match)
            return true
        }
        XCTAssertEqual(traceStats.matchesFound, 2)
        XCTAssertEqual(matches.map(\.lineNumber), [1, 3])
        XCTAssertEqual(matches[0].fields.traceID, "req-1")

        let token = CancelToken()
        token.cancel()
        XCTAssertTrue(token.isCancelled)
        XCTAssertThrowsError(try file.search(SearchQuery(), cancelToken: token)) { err in
            guard case LogloomError.cancelled = err else {
                XCTFail("Expected cancelled, got \(err)")
                return
            }
        }
    }

    func testLinePageCachePreIndexAndBoundedLRUEviction() throws {
        var rows: [String] = []
        rows.reserveCapacity(120)
        for i in 1...120 {
            rows.append("{\"timestamp\":\"2026-01-01T00:00:\(String(format: "%02d", i % 60))Z\",\"level\":\"INFO\",\"request_id\":\"tr-\(i)\",\"msg\":\"line \(i)\"}")
        }
        let url = try makeFixture(rows.joined(separator: "\n") + "\n")

        let file = try LogFile(path: url.path)
        let head = try file.readHead(maxLines: 15)

        let cache = LinePageCache(pageSize: 10, maxCachedPages: 3)
        cache.configure(file: file, headLines: head)

        XCTAssertFalse(file.isIndexed)
        XCTAssertEqual(cache.rowCount, 15)
        XCTAssertEqual(cache.line(atRow: 0)?.lineNumber, 1)
        XCTAssertEqual(cache.line(atRow: 14)?.lineNumber, 15)
        XCTAssertNil(cache.line(atRow: 15))
        XCTAssertEqual(cache.cachedPageCount, 0)

        let stats = try file.buildIndex()
        XCTAssertEqual(stats.totalLines, 120)
        cache.clearCache()

        XCTAssertEqual(cache.rowCount, 120)
        XCTAssertEqual(cache.line(atRow: 0)?.fields.traceID, "tr-1")
        XCTAssertEqual(cache.line(atRow: 15)?.lineNumber, 16)
        XCTAssertEqual(cache.line(atRow: 35)?.lineNumber, 36)
        XCTAssertEqual(cache.cachedPageCount, 3)

        // Touch page 0 so page 1 becomes least recently used, then load page 11.
        XCTAssertEqual(cache.line(atRow: 2)?.lineNumber, 3)
        XCTAssertEqual(cache.line(atRow: 119)?.lineNumber, 120)
        XCTAssertEqual(cache.cachedPageCount, 3)
    }

    func testViewportStoreFullAndFilteredModes() throws {
        var rows: [String] = []
        rows.reserveCapacity(30)
        for i in 1...30 {
            let level = (i % 5 == 0) ? "ERROR" : "INFO"
            let trace = (i % 3 == 0) ? "req-alpha" : "req-beta"
            rows.append("{\"timestamp\":\"2026-11-26T10:00:\(String(format: "%02d", i % 60))Z\",\"level\":\"\(level)\",\"request_id\":\"\(trace)\",\"msg\":\"event \(i)\"}")
        }
        let url = try makeFixture(rows.joined(separator: "\n") + "\n")
        let file = try LogFile(path: url.path)
        let head = try file.readHead(maxLines: 5)

        let store = ViewportStore(pageCache: LinePageCache(pageSize: 10, maxCachedPages: 4))
        store.configureFile(file, headLines: head)
        XCTAssertEqual(store.mode, .fullFile)
        XCTAssertEqual(store.rowCount, 5)
        XCTAssertEqual(store.rowIndex(forLineNumber: 4), 3)
        XCTAssertNil(store.rowIndex(forLineNumber: 10))

        _ = try file.buildIndex()
        store.pageCache.clearCache()
        XCTAssertEqual(store.rowCount, 30)
        XCTAssertEqual(store.rowIndex(forLineNumber: 25), 24)

        var matches: [SearchMatch] = []
        _ = try file.search(SearchQuery(traceID: "req-alpha")) { match in
            matches.append(match)
            return true
        }
        store.beginFilteredMode()
        store.appendFilteredMatches(matches)

        XCTAssertEqual(store.mode, .filtered)
        XCTAssertEqual(store.rowCount, 10)
        XCTAssertEqual(store.line(atRow: 0)?.lineNumber, 3)
        XCTAssertEqual(store.line(atRow: 9)?.lineNumber, 30)
        XCTAssertEqual(store.rowIndex(forLineNumber: 15), 4)
        XCTAssertNil(store.rowIndex(forLineNumber: 14))
        XCTAssertNil(store.rowIndex(forLineNumber: 0))

        store.setFullFileMode()
        XCTAssertEqual(store.mode, .fullFile)
        XCTAssertEqual(store.rowCount, 30)
        XCTAssertEqual(store.rowIndex(forLineNumber: 15), 14)
    }

    func testTraceIsolationAndInvalidRegex() throws {
        let payload = """
        {"timestamp":"2026-11-26T10:00:00Z","level":"INFO","request_id":"tr-1","msg":"Alpha start"}
        {"timestamp":"2026-11-26T10:00:01Z","level":"INFO","request_id":"tr-2","msg":"Other trace"}
        {"timestamp":"2026-11-26T10:00:02Z","level":"ERROR","request_id":"tr-1","msg":"TIMEOUT waiting db"}
        {"timestamp":"2026-11-26T10:00:03Z","level":"WARN","request_id":"tr-1","msg":"Timeout retry"}
        """
        let url = try makeFixture(payload)
        let file = try LogFile(path: url.path)

        var traceMatches: [SearchMatch] = []
        let stats = try file.search(SearchQuery(traceID: "tr-1")) { match in
            traceMatches.append(match)
            return true
        }
        XCTAssertEqual(stats.matchesFound, 3)
        XCTAssertEqual(traceMatches.map(\.lineNumber), [1, 3, 4])

        var filteredMatches: [SearchMatch] = []
        _ = try file.search(
            SearchQuery(traceID: "tr-1", level: "ERROR", textQuery: "timeout", caseSensitive: false)
        ) { match in
            filteredMatches.append(match)
            return true
        }
        XCTAssertEqual(filteredMatches.map(\.lineNumber), [3])

        XCTAssertThrowsError(
            try file.search(SearchQuery(textQuery: "[unclosed", isRegex: true))
        ) { err in
            guard case LogloomError.invalidRegex = err else {
                XCTFail("Expected invalidRegex, got \(err)")
                return
            }
        }
    }
}
