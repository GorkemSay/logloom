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

  let viewportStore = ViewportStore()

  private static let interactiveMatchLimit: UInt64 = 100_000

  private var scopeGuard: SecurityScopeGuard?
  private var openedFile: LogFile?
  private var openGeneration: UInt64 = 0
  private var indexCancelToken: CancelToken?
  private var indexTask: Task<Void, Never>?
  private var searchGeneration: UInt64 = 0
  private var searchCancelToken: CancelToken?
  private var searchTask: Task<Void, Never>?

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
    let generation = openGeneration
    indexCancelToken?.cancel()
    indexTask?.cancel()
    cancelActiveSearch()

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

      resetFilterState()
      self.viewportStore.configureFile(file, headLines: head)
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
    cancelActiveSearch()

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

    resetFilterState()
    viewportStore.configureFile(nil, headLines: [])
    viewportRevision &+= 1
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
    cancelActiveSearch()
    let hadFilters = isFilterActive || viewportStore.mode == .filtered || searchError != nil
    resetFilterState()
    viewportStore.setFullFileMode()
    if hadFilters {
      viewportRevision &+= 1
    }
  }

  func scheduleSearch(immediate: Bool) {
    cancelActiveSearch()

    guard let file = openedFile, isFilterActive else {
      let needsReload = viewportStore.mode == .filtered || searchError != nil || searchStats != nil || searchMatchCount > 0
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
    let generation = searchGeneration
    let query = SearchQuery(
      traceID: isolatedTraceID,
      level: selectedLevel,
      textQuery: searchText.isEmpty ? nil : searchText,
      caseSensitive: isCaseSensitive,
      isRegex: isRegex,
      limit: Self.interactiveMatchLimit,
      numThreads: 0
    )

    isSearching = true
    searchError = nil
    searchLimitReached = false
    searchDurationMs = nil

    let token = CancelToken()
    searchCancelToken = token

    searchTask = Task.detached(priority: .userInitiated) { [weak self] in
      if !immediate {
        try? await Task.sleep(nanoseconds: 120_000_000)
      }
      if Task.isCancelled || token.isCancelled {
        return
      }

      await MainActor.run { [weak self] in
        guard let self,
          self.openGeneration == fileGeneration,
          self.searchGeneration == generation
        else { return }
        self.viewportStore.beginFilteredMode()
        self.searchMatchCount = 0
        self.viewportRevision &+= 1
      }

      if Task.isCancelled || token.isCancelled {
        return
      }

      let startTime = CFAbsoluteTimeGetCurrent()
      var batch: [SearchMatch] = []
      batch.reserveCapacity(200)
      var lastFlushTime = startTime

      do {
        let stats = try file.search(query, cancelToken: token) { match in
          if token.isCancelled {
            return false
          }
          batch.append(match)
          let now = CFAbsoluteTimeGetCurrent()
          if batch.count >= 200 || (now - lastFlushTime) >= 0.05 {
            let chunk = batch
            batch.removeAll(keepingCapacity: true)
            lastFlushTime = now
            Task { @MainActor [weak self] in
              guard let self,
                self.openGeneration == fileGeneration,
                self.searchGeneration == generation
              else { return }
              self.viewportStore.appendFilteredMatches(chunk)
              self.searchMatchCount = self.viewportStore.rowCount
              self.viewportRevision &+= 1
            }
          }
          return !token.isCancelled
        }

        let remaining = batch
        let elapsedMs = (CFAbsoluteTimeGetCurrent() - startTime) * 1000.0

        await MainActor.run { [weak self] in
          guard let self,
            self.openGeneration == fileGeneration,
            self.searchGeneration == generation
          else { return }
          if !remaining.isEmpty {
            self.viewportStore.appendFilteredMatches(remaining)
          }
          self.isSearching = false
          self.searchStats = stats
          self.searchMatchCount = self.viewportStore.rowCount
          self.searchLimitReached = stats.limitReached
          self.searchDurationMs = elapsedMs
          self.viewportRevision &+= 1
        }
      } catch {
        await MainActor.run { [weak self] in
          guard let self,
            self.openGeneration == fileGeneration,
            self.searchGeneration == generation
          else { return }
          self.isSearching = false
          if case LogloomError.cancelled = error {
            return
          }
          if token.isCancelled {
            return
          }
          self.searchError = error.localizedDescription
        }
      }
    }
  }

  private func cancelActiveSearch() {
    searchGeneration &+= 1
    searchCancelToken?.cancel()
    searchCancelToken = nil
    searchTask?.cancel()
    searchTask = nil
    isSearching = false
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

  private func startBackgroundIndexing(file: LogFile, generation: UInt64) {
    let token = CancelToken()
    self.indexCancelToken = token

    self.indexTask = Task.detached(priority: .userInitiated) { [weak self] in
      var lastProgressDispatch: CFAbsoluteTime = 0
      do {
        let stats = try file.buildIndex(cancelToken: token) {
          bytesProcessed, bytesTotal, linesIndexed in
          let now = CFAbsoluteTimeGetCurrent()
          if now - lastProgressDispatch >= 0.05 {
            lastProgressDispatch = now
            let ratio: Double =
              bytesTotal > 0
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
          self.viewportStore.pageCache.clearCache()
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
