import Foundation
import LogloomCore
import XCTest

private struct MockUpdateHTTPClient: UpdateHTTPClient {
    let statusCode: Int
    let data: Data
    let errorToThrow: Error?

    init(statusCode: Int = 200, json: String = "", error: Error? = nil) {
        self.statusCode = statusCode
        self.data = Data(json.utf8)
        self.errorToThrow = error
    }

    func data(for request: URLRequest) async throws -> (Data, HTTPURLResponse) {
        if let errorToThrow {
            throw errorToThrow
        }
        let url = request.url ?? URL(string: "https://api.github.com")!
        let response = HTTPURLResponse(
            url: url,
            statusCode: statusCode,
            httpVersion: "HTTP/1.1",
            headerFields: nil
        )!
        return (data, response)
    }
}

final class UpdateCheckerTests: XCTestCase {
    func testSemVerParsingAndOrdering() {
        XCTAssertEqual(SemVer(parsing: "v1.0.0")?.description, "1.0.0")
        XCTAssertEqual(SemVer(parsing: "V1.2")?.description, "1.2.0")
        XCTAssertEqual(SemVer(parsing: "1.2.3+build.45")?.description, "1.2.3")
        XCTAssertNil(SemVer(parsing: ""))
        XCTAssertNil(SemVer(parsing: "1"))
        XCTAssertNil(SemVer(parsing: "1.2.3-"))
        XCTAssertNil(SemVer(parsing: "abc"))

        XCTAssertLessThan(SemVer(parsing: "1.2.0")!, SemVer(parsing: "1.10.0")!)
        XCTAssertLessThan(SemVer(parsing: "1.1.0-beta.1")!, SemVer(parsing: "1.1.0-beta.2")!)
        XCTAssertLessThan(SemVer(parsing: "1.1.0-beta.2")!, SemVer(parsing: "1.1.0")!)
    }

    func testUpdateAvailableSelectsPreferredMacAsset() async throws {
        let json = """
        {
          "tag_name": "v1.2.0",
          "name": "Logloom 1.2.0",
          "body": "- Faster search\\n- In-app updater",
          "html_url": "https://github.com/GorkemSay/logloom/releases/tag/v1.2.0",
          "published_at": "2026-11-27T12:00:00Z",
          "assets": [
            {
              "name": "Logloom-1.2.0-win64.exe",
              "browser_download_url": "https://example.com/Logloom-1.2.0-win64.exe",
              "size": 1000,
              "content_type": "application/octet-stream"
            },
            {
              "name": "Logloom-1.2.0-macos.zip",
              "browser_download_url": "https://example.com/Logloom-1.2.0-macos.zip",
              "size": 2000,
              "content_type": "application/zip"
            },
            {
              "name": "Logloom-1.2.0-macos-arm64.dmg",
              "browser_download_url": "https://example.com/Logloom-1.2.0-macos-arm64.dmg",
              "size": 3000,
              "content_type": "application/x-apple-diskimage"
            }
          ]
        }
        """
        let checker = UpdateChecker(httpClient: MockUpdateHTTPClient(json: json))
        let outcome = try await checker.checkForUpdate(currentVersionString: "1.0.0")

        guard case .updateAvailable(let current, let release) = outcome else {
            return XCTFail("Expected .updateAvailable, got \(outcome)")
        }
        XCTAssertEqual(current, SemVer(parsing: "1.0.0"))
        XCTAssertEqual(release.version, SemVer(parsing: "1.2.0"))
        XCTAssertEqual(release.title, "Logloom 1.2.0")
        XCTAssertEqual(release.preferredAsset?.name, "Logloom-1.2.0-macos-arm64.dmg")
    }

    func testUpToDateWhenRemoteMatchesOrIsOlder() async throws {
        let json = """
        {
          "tag_name": "v1.0.0",
          "name": "Logloom 1.0.0",
          "body": "",
          "html_url": "https://github.com/GorkemSay/logloom/releases/tag/v1.0.0",
          "assets": []
        }
        """
        let checker = UpdateChecker(httpClient: MockUpdateHTTPClient(json: json))
        let outcome = try await checker.checkForUpdate(currentVersionString: "1.0.0")
        XCTAssertEqual(outcome, .upToDate(currentVersion: SemVer(parsing: "1.0.0")!))
    }

    func testHttp404MapsToNoReleasesPublished() async throws {
        let checker = UpdateChecker(httpClient: MockUpdateHTTPClient(statusCode: 404, json: "{}"))
        let outcome = try await checker.checkForUpdate(currentVersionString: "1.0.0")
        XCTAssertEqual(
            outcome,
            .noReleasesPublished(
                currentVersion: SemVer(parsing: "1.0.0")!,
                releasesPageURL: URL(string: "https://github.com/GorkemSay/logloom/releases")!
            )
        )
    }

    func testHttp500ThrowsHttpStatusError() async {
        let checker = UpdateChecker(httpClient: MockUpdateHTTPClient(statusCode: 500, json: "{}"))
        do {
            _ = try await checker.checkForUpdate(currentVersionString: "1.0.0")
            XCTFail("Expected UpdateCheckError.httpStatus(500)")
        } catch let error as UpdateCheckError {
            XCTAssertEqual(error, .httpStatus(500))
        } catch {
            XCTFail("Unexpected error type: \(error)")
        }
    }
}
