import Foundation

/// Parsed semantic version supporting optional leading `v`/`V`, two- or three-part numbers,
/// optional `-prerelease`, and ignored `+build` metadata.
public struct SemVer: Comparable, Equatable, Hashable, Sendable, CustomStringConvertible {
    public let major: Int
    public let minor: Int
    public let patch: Int
    public let prerelease: String?

    public init?(parsing raw: String) {
        var text = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return nil }
        if text.hasPrefix("v") || text.hasPrefix("V") {
            text.removeFirst()
        }
        if let plusIndex = text.firstIndex(of: "+") {
            text = String(text[..<plusIndex])
        }

        let corePart: String
        let prereleasePart: String?
        if let dashIndex = text.firstIndex(of: "-") {
            corePart = String(text[..<dashIndex])
            let suffix = String(text[text.index(after: dashIndex)...])
            guard !suffix.isEmpty else { return nil }
            prereleasePart = suffix
        } else {
            corePart = text
            prereleasePart = nil
        }

        let parts = corePart.split(separator: ".", omittingEmptySubsequences: false)
        guard parts.count == 2 || parts.count == 3 else { return nil }
        guard let major = Int(parts[0]), major >= 0,
              let minor = Int(parts[1]), minor >= 0 else {
            return nil
        }
        let patch: Int
        if parts.count == 3 {
            guard let parsedPatch = Int(parts[2]), parsedPatch >= 0 else { return nil }
            patch = parsedPatch
        } else {
            patch = 0
        }

        self.major = major
        self.minor = minor
        self.patch = patch
        self.prerelease = prereleasePart
    }

    public var description: String {
        if let prerelease {
            return "\(major).\(minor).\(patch)-\(prerelease)"
        }
        return "\(major).\(minor).\(patch)"
    }

    public static func < (lhs: SemVer, rhs: SemVer) -> Bool {
        if lhs.major != rhs.major { return lhs.major < rhs.major }
        if lhs.minor != rhs.minor { return lhs.minor < rhs.minor }
        if lhs.patch != rhs.patch { return lhs.patch < rhs.patch }

        switch (lhs.prerelease, rhs.prerelease) {
        case (nil, nil):
            return false
        case (nil, .some):
            return false
        case (.some, nil):
            return true
        case let (.some(leftPre), .some(rightPre)):
            return comparePrerelease(leftPre, rightPre)
        }
    }

    private static func comparePrerelease(_ lhs: String, _ rhs: String) -> Bool {
        let leftParts = lhs.split(separator: ".", omittingEmptySubsequences: false)
        let rightParts = rhs.split(separator: ".", omittingEmptySubsequences: false)
        for (l, r) in zip(leftParts, rightParts) where l != r {
            if let lInt = Int(l), let rInt = Int(r) {
                return lInt < rInt
            }
            if Int(l) != nil {
                return true
            }
            if Int(r) != nil {
                return false
            }
            return String(l) < String(r)
        }
        return leftParts.count < rightParts.count
    }
}

public struct ReleaseAsset: Equatable, Sendable {
    public let name: String
    public let downloadURL: URL
    public let sizeBytes: Int64
    public let contentType: String?

    public init(name: String, downloadURL: URL, sizeBytes: Int64, contentType: String?) {
        self.name = name
        self.downloadURL = downloadURL
        self.sizeBytes = sizeBytes
        self.contentType = contentType
    }
}

public struct ReleaseInfo: Equatable, Sendable {
    public let version: SemVer
    public let tagName: String
    public let title: String
    public let releaseNotes: String
    public let publishedAt: Date?
    public let htmlURL: URL
    public let preferredAsset: ReleaseAsset?

    public init(
        version: SemVer,
        tagName: String,
        title: String,
        releaseNotes: String,
        publishedAt: Date?,
        htmlURL: URL,
        preferredAsset: ReleaseAsset?
    ) {
        self.version = version
        self.tagName = tagName
        self.title = title
        self.releaseNotes = releaseNotes
        self.publishedAt = publishedAt
        self.htmlURL = htmlURL
        self.preferredAsset = preferredAsset
    }
}

public enum UpdateCheckOutcome: Equatable, Sendable {
    case upToDate(currentVersion: SemVer)
    case updateAvailable(currentVersion: SemVer, release: ReleaseInfo)
    case noReleasesPublished(currentVersion: SemVer, releasesPageURL: URL)
}

public enum UpdateCheckError: LocalizedError, Equatable, Sendable {
    case invalidEndpoint
    case networkFailure(String)
    case httpStatus(Int)
    case invalidResponse(String)

    public var errorDescription: String? {
        switch self {
        case .invalidEndpoint:
            return "Update endpoint URL is invalid."
        case .networkFailure(let message):
            return message
        case .httpStatus(let code):
            return "GitHub returned HTTP status \(code)."
        case .invalidResponse(let message):
            return message
        }
    }
}

public protocol UpdateHTTPClient: Sendable {
    func data(for request: URLRequest) async throws -> (Data, HTTPURLResponse)
}

public struct URLSessionUpdateHTTPClient: UpdateHTTPClient {
    private let session: URLSession

    public init(session: URLSession = .shared) {
        self.session = session
    }

    public func data(for request: URLRequest) async throws -> (Data, HTTPURLResponse) {
        do {
            let (data, response) = try await session.data(for: request)
            guard let httpResponse = response as? HTTPURLResponse else {
                throw UpdateCheckError.invalidResponse("Response was not an HTTP response.")
            }
            return (data, httpResponse)
        } catch let error as UpdateCheckError {
            throw error
        } catch {
            throw UpdateCheckError.networkFailure(error.localizedDescription)
        }
    }
}

public struct UpdateChecker: Sendable {
    public let repository: String
    private let httpClient: any UpdateHTTPClient

    public init(
        repository: String = "GorkemSay/logloom",
        httpClient: any UpdateHTTPClient = URLSessionUpdateHTTPClient()
    ) {
        self.repository = repository
        self.httpClient = httpClient
    }

    public var releasesPageURL: URL {
        URL(string: "https://github.com/\(repository)/releases")
            ?? URL(string: "https://github.com")!
    }

    private var latestReleaseEndpointURL: URL? {
        URL(string: "https://api.github.com/repos/\(repository)/releases/latest")
    }

    public func checkForUpdate(currentVersionString: String) async throws -> UpdateCheckOutcome {
        guard let currentVersion = SemVer(parsing: currentVersionString) else {
            throw UpdateCheckError.invalidResponse("Current app version '\(currentVersionString)' is not valid.")
        }
        guard let endpointURL = latestReleaseEndpointURL else {
            throw UpdateCheckError.invalidEndpoint
        }

        var request = URLRequest(
            url: endpointURL,
            cachePolicy: .reloadIgnoringLocalCacheData,
            timeoutInterval: 10
        )
        request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        request.setValue("Logloom/\(currentVersion.description)", forHTTPHeaderField: "User-Agent")

        let (data, response) = try await httpClient.data(for: request)
        if response.statusCode == 404 {
            return .noReleasesPublished(currentVersion: currentVersion, releasesPageURL: releasesPageURL)
        }
        guard (200...299).contains(response.statusCode) else {
            throw UpdateCheckError.httpStatus(response.statusCode)
        }

        let payload: GitHubReleasePayload
        do {
            payload = try JSONDecoder().decode(GitHubReleasePayload.self, from: data)
        } catch {
            throw UpdateCheckError.invalidResponse("Failed to decode release metadata.")
        }

        guard let latestVersion = SemVer(parsing: payload.tagName) else {
            throw UpdateCheckError.invalidResponse("Release tag '\(payload.tagName)' is not a valid version.")
        }

        guard latestVersion > currentVersion else {
            return .upToDate(currentVersion: currentVersion)
        }

        let htmlURL = payload.htmlURL.flatMap(URL.init(string:)) ?? releasesPageURL
        let publishedAt = payload.publishedAt.flatMap { ISO8601DateFormatter().date(from: $0) }
        let mappedAssets = (payload.assets ?? []).compactMap { asset -> ReleaseAsset? in
            guard let url = URL(string: asset.browserDownloadURL) else { return nil }
            return ReleaseAsset(
                name: asset.name,
                downloadURL: url,
                sizeBytes: asset.size,
                contentType: asset.contentType
            )
        }
        let releaseTitle = payload.name.flatMap { $0.isEmpty ? nil : $0 } ?? payload.tagName
        let releaseNotes = (payload.body ?? "").trimmingCharacters(in: .whitespacesAndNewlines)

        let release = ReleaseInfo(
            version: latestVersion,
            tagName: payload.tagName,
            title: releaseTitle,
            releaseNotes: releaseNotes,
            publishedAt: publishedAt,
            htmlURL: htmlURL,
            preferredAsset: Self.selectPreferredMacAsset(from: mappedAssets)
        )
        return .updateAvailable(currentVersion: currentVersion, release: release)
    }

    public static func selectPreferredMacAsset(from assets: [ReleaseAsset]) -> ReleaseAsset? {
        let excludedExtensions = [".exe", ".msi", ".appimage", ".deb", ".rpm", ".tar.gz", ".tar.xz"]
        let candidates = assets.filter { asset in
            let lower = asset.name.lowercased()
            return !excludedExtensions.contains(where: { lower.hasSuffix($0) })
        }
        return candidates.max { assetScore($0.name) < assetScore($1.name) }
            .flatMap { assetScore($0.name) > 0 ? $0 : nil }
    }

    private static func assetScore(_ name: String) -> Int {
        let lower = name.lowercased()
        let extensionScore: Int
        if lower.hasSuffix(".dmg") {
            extensionScore = 300
        } else if lower.hasSuffix(".pkg") {
            extensionScore = 200
        } else if lower.hasSuffix(".zip") {
            extensionScore = 100
        } else {
            return 0
        }

        var platformBonus = 0
        if lower.contains("macos") || lower.contains("darwin") || lower.contains("osx") {
            platformBonus += 30
        }
        if lower.contains("universal") || lower.contains("arm64") {
            platformBonus += 20
        } else if lower.contains("linux") || lower.contains("windows") || lower.contains("win64") {
            return 0
        }
        return extensionScore + platformBonus
    }
}

private struct GitHubReleasePayload: Decodable {
    let tagName: String
    let name: String?
    let body: String?
    let htmlURL: String?
    let publishedAt: String?
    let assets: [GitHubAssetPayload]?

    enum CodingKeys: String, CodingKey {
        case tagName = "tag_name"
        case name
        case body
        case htmlURL = "html_url"
        case publishedAt = "published_at"
        case assets
    }
}

private struct GitHubAssetPayload: Decodable {
    let name: String
    let browserDownloadURL: String
    let size: Int64
    let contentType: String?

    enum CodingKeys: String, CodingKey {
        case name
        case browserDownloadURL = "browser_download_url"
        case size
        case contentType = "content_type"
    }
}
