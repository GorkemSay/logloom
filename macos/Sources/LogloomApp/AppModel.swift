import AppKit
import Foundation
import LogloomCore
import UniformTypeIdentifiers

private final class SecurityScopeGuard {
    let url: URL
    private let didStartAccess: Bool

    init(url: URL) {
        self.url = url
        self.didStartAccess = url.startAccessingSecurityScopedResource()
    }

    deinit {
        if didStartAccess {
            url.stopAccessingSecurityScopedResource()
        }
    }
}

@MainActor
final class AppModel: ObservableObject {
    @Published private(set) var fileName: String?
    @Published private(set) var filePath: String?
    @Published private(set) var fileSize: UInt64 = 0
    @Published private(set) var isIndexed: Bool = false
    @Published private(set) var isIndexing: Bool = false
    @Published private(set) var indexProgress: Double = 0
    @Published private(set) var indexedLinesSoFar: UInt64 = 0
    @Published private(set) var indexStats: IndexStats?
    @Published private(set) var hasTruncatedFinalLine: Bool = false
    @Published private(set) var oversizedLineCount: UInt64 = 0
    @Published var truncatedBannerDismissed: Bool = false
    @Published var selectedLineNumber: UInt64?
    @Published private(set) var viewportRevision: UInt64 = 0
    @Published var errorMessage: String?

    let lineCache = LinePageCache()

    private var scopeGuard: SecurityScopeGuard?
    private var openedFile: LogFile?
    private var openGeneration: UInt64 = 0
    private var indexCancelToken: CancelToken?
    private var indexTask: Task<Void, Never>?

    var hasOpenFile: Bool {
        openedFile != nil
    }

    var rowCount: Int {
        lineCache.rowCount
    }

    var formattedFileSize: String {
        ByteCountFormatter.string(fromByteCount: Int64(clamping: fileSize), countStyle: .file)
    }

    func openPanel() {
        let panel = NSOpenPanel()
        panel.title = "Open Log File"
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.canCreateDirectories = false

        var types: [UTType] = [.plainText, .log]
        if let json = UTType(filenameExtension: "jsonl") {
            types.append(json)
        }
        if let ndjson = UTType(filenameExtension: "ndjson") {
            types.append(ndjson)
        }
        panel.allowedContentTypes = types

        if panel.runModal() == .OK, let url = panel.url {
            openFile(url: url)
        }
    }

    func openFile(url: URL) {
        guard url.isFileURL else {
            errorMessage = "Only local files are supported."
            return
        }

        openGeneration &+= 1
        let generation = openGeneration
        indexCancelToken?.cancel()
        indexTask?.cancel()

        let guardScope = SecurityScopeGuard(url: url)
        do {
            let file = try LogFile(path: url.path)
            let head = try file.readHead(maxLines: 500)

            self.openedFile = file
            self.scopeGuard = guardScope
            self.fileName = url.lastPathComponent
            self.filePath = url.path
            self.fileSize = file.fileSize
            self.isIndexed = false
            self.isIndexing = true
            self.indexProgress = 0
            self.indexedLinesSoFar = UInt64(head.count)
            self.indexStats = nil
            self.hasTruncatedFinalLine = false
            self.oversizedLineCount = 0
            self.truncatedBannerDismissed = false
            self.selectedLineNumber = nil
            self.errorMessage = nil

            self.lineCache.configure(file: file, headLines: head)
            self.viewportRevision &+= 1

            startBackgroundIndexing(file: file, generation: generation)
        } catch {
            self.errorMessage = error.localizedDescription
        }
    }

    func closeFile() {
        openGeneration &+= 1
        indexCancelToken?.cancel()
        indexCancelToken = nil
        indexTask?.cancel()
        indexTask = nil

        openedFile = nil
        scopeGuard = nil
        fileName = nil
        filePath = nil
        fileSize = 0
        isIndexed = false
        isIndexing = false
        indexProgress = 0
        indexedLinesSoFar = 0
        indexStats = nil
        hasTruncatedFinalLine = false
        oversizedLineCount = 0
        truncatedBannerDismissed = false
        selectedLineNumber = nil
        errorMessage = nil

        lineCache.configure(file: nil, headLines: [])
        viewportRevision &+= 1
    }

    private func startBackgroundIndexing(file: LogFile, generation: UInt64) {
        let token = CancelToken()
        self.indexCancelToken = token

        self.indexTask = Task.detached(priority: .userInitiated) { [weak self] in
            var lastProgressDispatch: CFAbsoluteTime = 0
            do {
                let stats = try file.buildIndex(cancelToken: token) { bytesProcessed, bytesTotal, linesIndexed in
                    let now = CFAbsoluteTimeGetCurrent()
                    if now - lastProgressDispatch >= 0.05 {
                        lastProgressDispatch = now
                        let ratio: Double = bytesTotal > 0
                            ? min(1.0, max(0.0, Double(bytesProcessed) / Double(bytesTotal)))
                            : 1.0
                        Task { @MainActor [weak self] in
                            guard let self, self.openGeneration == generation else { return }
                            self.indexedLinesSoFar = linesIndexed
                            self.indexProgress = ratio
                        }
                    }
                    return !token.isCancelled
                }

                await MainActor.run { [weak self] in
                    guard let self, self.openGeneration == generation else { return }
                    self.isIndexing = false
                    self.isIndexed = true
                    self.indexProgress = 1.0
                    self.indexedLinesSoFar = stats.totalLines
                    self.indexStats = stats
                    self.hasTruncatedFinalLine = stats.hasTruncatedFinalLine
                    self.oversizedLineCount = stats.oversizedLines
                    self.lineCache.clearCache()
                    self.viewportRevision &+= 1
                }
            } catch {
                await MainActor.run { [weak self] in
                    guard let self, self.openGeneration == generation else { return }
                    self.isIndexing = false
                    if case LogloomError.cancelled = error {
                        return
                    }
                    self.errorMessage = error.localizedDescription
                }
            }
        }
    }
}
