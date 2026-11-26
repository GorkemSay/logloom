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
}
