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

    @Published var searchText: String = ""
    @Published var isRegex: Bool = false
    @Published var isCaseSensitive: Bool = false
    @Published var selectedLevel: String? = nil
    @Published private(set) var isolatedTraceID: String? = nil
    @Published private(set) var isSearching: Bool = false
    @Published private(set) var searchError: String? = nil
    @Published private(set) var searchMatchCount: Int = 0
    @Published private(set) var searchStats: SearchStats? = nil
    @Published private(set) var searchLimitReached: Bool = false
    @Published private(set) var searchDurationMs: Double? = nil
    @Published private(set) var focusSearchTrigger: UInt64 = 0

    @Published var isInspectorVisible: Bool = true
    @Published private(set) var selectedLine: LogLine? = nil
    @Published private(set) var contextLines: [LogLine] = []
    @Published var contextRadius: Int = 10
    @Published var isJumpToLinePresented: Bool = false
    @Published private(set) var isExporting: Bool = false
    @Published private(set) var exportStatusMessage: String? = nil
    @Published private(set) var scrollToSelectionTrigger: UInt64 = 0

    let viewportStore = ViewportStore()

    private static let interactiveMatchLimit: UInt64 = 100_000

    private let indexingController = IndexingController()
    private let searchController = SearchController()
    private let exportController = ExportController()

    private var scopeGuard: SecurityScopeGuard?
    private var openedFile: LogFile?
    private var openGeneration: UInt64 = 0

    var hasOpenFile: Bool {
        openedFile != nil
    }

    var rowCount: Int {
        viewportStore.rowCount
    }

    var isFilterActive: Bool {
        !searchText.isEmpty || selectedLevel != nil || isolatedTraceID != nil
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
        let fileGeneration = openGeneration
        indexingController.cancel()
        searchController.cancel()

        let guardScope = SecurityScopeGuard(url: url)
        do {
            let file = try LogFile(path: url.path)
            let head = try file.readHead(maxLines: 500)

            openedFile = file
            scopeGuard = guardScope
            fileName = url.lastPathComponent
            filePath = url.path
            fileSize = file.fileSize
            isIndexed = false
            isIndexing = true
            indexProgress = 0
            indexedLinesSoFar = UInt64(head.count)
            indexStats = nil
            hasTruncatedFinalLine = false
            oversizedLineCount = 0
            truncatedBannerDismissed = false
            selectedLineNumber = nil
            selectedLine = nil
            contextLines = []
            isJumpToLinePresented = false
            isExporting = false
            exportStatusMessage = nil
            errorMessage = nil

            resetFilterState()
            viewportStore.configureFile(file, headLines: head)
            viewportRevision &+= 1

            startIndexing(file: file, fileGeneration: fileGeneration)
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    func closeFile() {
        openGeneration &+= 1
        indexingController.cancel()
        searchController.cancel()

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
        selectedLine = nil
        contextLines = []
        isJumpToLinePresented = false
        isExporting = false
        exportStatusMessage = nil
        errorMessage = nil

        resetFilterState()
        viewportStore.configureFile(nil, headLines: [])
        viewportRevision &+= 1
    }

    func refreshSelectedLineAndContext() {
        guard let lineNum = selectedLineNumber else {
            selectedLine = nil
            contextLines = []
            return
        }

        if let row = viewportStore.rowIndex(forLineNumber: lineNum),
            let cached = viewportStore.line(atRow: row)
        {
            selectedLine = cached
        } else if isIndexed, let file = openedFile {
            selectedLine = try? file.readLine(lineNum)
        } else {
            selectedLine = nil
        }

        if isInspectorVisible, isIndexed, let file = openedFile {
            let radius = UInt64(max(1, contextRadius))
            contextLines = (try? file.readContext(around: lineNum, before: radius, after: radius)) ?? []
        } else {
            contextLines = []
        }
    }

    func toggleInspector() {
        isInspectorVisible.toggle()
        refreshSelectedLineAndContext()
    }

    func selectNextMatch() {
        let count = viewportStore.rowCount
        guard count > 0 else { return }
        let currentRow = selectedLineNumber.flatMap { viewportStore.rowIndex(forLineNumber: $0) }
        let nextRow = currentRow.map { ($0 + 1) % count } ?? 0
        if let line = viewportStore.line(atRow: nextRow) {
            selectedLineNumber = line.lineNumber
            scrollToSelectionTrigger &+= 1
            refreshSelectedLineAndContext()
        }
    }

    func selectPreviousMatch() {
        let count = viewportStore.rowCount
        guard count > 0 else { return }
        let currentRow = selectedLineNumber.flatMap { viewportStore.rowIndex(forLineNumber: $0) }
        let prevRow = currentRow.map { ($0 - 1 + count) % count } ?? (count - 1)
        if let line = viewportStore.line(atRow: prevRow) {
            selectedLineNumber = line.lineNumber
            scrollToSelectionTrigger &+= 1
            refreshSelectedLineAndContext()
        }
    }

    func requestJumpToLine() {
        guard hasOpenFile else { return }
        isJumpToLinePresented = true
    }

    func jumpToLine(_ lineNumber: UInt64, clearFiltersIfNeeded: Bool = true) {
        guard lineNumber > 0 else { return }
        let maxAvailable = isIndexed
            ? (openedFile?.lineCount ?? 0)
            : UInt64(viewportStore.pageCache.rowCount)
        guard maxAvailable > 0 else { return }

        let clampedLine = min(lineNumber, maxAvailable)
        if viewportStore.mode == .filtered,
            viewportStore.rowIndex(forLineNumber: clampedLine) == nil,
            clearFiltersIfNeeded
        {
            clearAllFilters()
        }

        selectedLineNumber = clampedLine
        scrollToSelectionTrigger &+= 1
        refreshSelectedLineAndContext()
    }

    func exportFilteredPanel() {
        guard let file = openedFile, isFilterActive, !isExporting else { return }

        let fileGeneration = openGeneration
        let query = makeSearchQuery(limit: 0)
        exportController.exportFiltered(
            file: file,
            sourcePath: filePath,
            fileName: fileName,
            isolatedTraceID: isolatedTraceID,
            query: query,
            onStart: { [weak self] in
                guard let self, self.openGeneration == fileGeneration else { return }
                self.isExporting = true
                self.exportStatusMessage = "Exporting..."
                self.errorMessage = nil
            },
            onComplete: { [weak self] count, destName in
                guard let self, self.openGeneration == fileGeneration else { return }
                self.isExporting = false
                self.exportStatusMessage = "Exported \(count) line(s) to \(destName)"
            },
            onError: { [weak self] error in
                guard let self, self.openGeneration == fileGeneration else { return }
                self.isExporting = false
                self.exportStatusMessage = nil
                self.errorMessage = error.localizedDescription
            }
        )
    }

    func requestSearchFocus() {
        guard hasOpenFile else { return }
        focusSearchTrigger &+= 1
    }

    func isolateTrace(_ traceID: String) {
        guard !traceID.isEmpty else { return }
        isolatedTraceID = traceID
        scheduleSearch(immediate: true)
    }

    func clearTraceIsolation() {
        guard isolatedTraceID != nil else { return }
        isolatedTraceID = nil
        scheduleSearch(immediate: true)
    }

    func clearAllFilters() {
        searchController.cancel()
        let hadFilters = isFilterActive || viewportStore.mode == .filtered || searchError != nil
        resetFilterState()
        viewportStore.setFullFileMode()
        if hadFilters {
            viewportRevision &+= 1
        }
    }

    func scheduleSearch(immediate: Bool) {
        searchController.cancel()

        guard let file = openedFile, isFilterActive else {
            let needsReload = viewportStore.mode == .filtered
                || searchError != nil
                || searchStats != nil
                || searchMatchCount > 0
            searchError = nil
            searchStats = nil
            searchMatchCount = 0
            searchLimitReached = false
            searchDurationMs = nil
            isSearching = false
            if viewportStore.mode == .filtered {
                viewportStore.setFullFileMode()
            }
            if needsReload {
                viewportRevision &+= 1
            }
            return
        }

        let fileGeneration = openGeneration
        let query = makeSearchQuery(limit: Self.interactiveMatchLimit)

        isSearching = true
        searchError = nil
        searchLimitReached = false
        searchDurationMs = nil

        searchController.start(
            file: file,
            query: query,
            immediate: immediate,
            onBegin: { [weak self] in
                guard let self, self.openGeneration == fileGeneration else { return }
                self.viewportStore.beginFilteredMode()
                self.searchMatchCount = 0
                self.viewportRevision &+= 1
            },
            onBatch: { [weak self] chunk in
                guard let self, self.openGeneration == fileGeneration else { return }
                self.viewportStore.appendFilteredMatches(chunk)
                self.searchMatchCount = self.viewportStore.rowCount
                self.viewportRevision &+= 1
            },
            onComplete: { [weak self] remaining, stats, elapsedMs in
                guard let self, self.openGeneration == fileGeneration else { return }
                if !remaining.isEmpty {
                    self.viewportStore.appendFilteredMatches(remaining)
                }
                self.isSearching = false
                self.searchStats = stats
                self.searchMatchCount = self.viewportStore.rowCount
                self.searchLimitReached = stats.limitReached
                self.searchDurationMs = elapsedMs
                self.viewportRevision &+= 1
            },
            onError: { [weak self] error in
                guard let self, self.openGeneration == fileGeneration else { return }
                self.isSearching = false
                self.searchError = error.localizedDescription
            }
        )
    }

    private func startIndexing(file: LogFile, fileGeneration: UInt64) {
        indexingController.start(
            file: file,
            onProgress: { [weak self] linesIndexed, ratio in
                guard let self, self.openGeneration == fileGeneration else { return }
                self.indexedLinesSoFar = linesIndexed
                self.indexProgress = ratio
            },
            onComplete: { [weak self] stats in
                guard let self, self.openGeneration == fileGeneration else { return }
                self.isIndexing = false
                self.isIndexed = true
                self.indexProgress = 1.0
                self.indexedLinesSoFar = stats.totalLines
                self.indexStats = stats
                self.hasTruncatedFinalLine = stats.hasTruncatedFinalLine
                self.oversizedLineCount = stats.oversizedLines
                self.viewportStore.pageCache.clearCache()
                self.viewportRevision &+= 1
                self.refreshSelectedLineAndContext()
            },
            onError: { [weak self] error in
                guard let self, self.openGeneration == fileGeneration else { return }
                self.isIndexing = false
                self.errorMessage = error.localizedDescription
            }
        )
    }

    private func makeSearchQuery(limit: UInt64) -> SearchQuery {
        SearchQuery(
            traceID: isolatedTraceID,
            level: selectedLevel,
            textQuery: searchText.isEmpty ? nil : searchText,
            caseSensitive: isCaseSensitive,
            isRegex: isRegex,
            limit: limit,
            numThreads: 0
        )
    }

    private func resetFilterState() {
        searchText = ""
        isRegex = false
        isCaseSensitive = false
        selectedLevel = nil
        isolatedTraceID = nil
        isSearching = false
        searchError = nil
        searchMatchCount = 0
        searchStats = nil
        searchLimitReached = false
        searchDurationMs = nil
    }
}
