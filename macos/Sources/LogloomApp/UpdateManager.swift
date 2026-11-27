import AppKit
import Foundation
import LogloomCore

enum UpdateUIState: Equatable {
  case idle
  case checking
  case upToDate(currentVersion: String)
  case updateAvailable(currentVersion: String, release: ReleaseInfo)
  case downloading(release: ReleaseInfo, progress: Double, bytesWritten: Int64, totalBytes: Int64)
  case readyToInstall(release: ReleaseInfo, localURL: URL)
  case noReleasesPublished(currentVersion: String, releasesPageURL: URL)
  case failed(message: String, releasesPageURL: URL)
}

@MainActor
final class UpdateManager: ObservableObject {
  @Published private(set) var state: UpdateUIState = .idle
  @Published var isSheetPresented: Bool = false
  @Published var isBannerDismissed: Bool = false

  private static let skippedVersionKey = "logloom.skippedUpdateVersion"

  private let checker: UpdateChecker
  private let currentVersionString: String
  private var hasCheckedOnLaunch = false
  private var checkTask: Task<Void, Never>?
  private var downloadController: UpdateDownloadController?

  init(
    bundle: Bundle = .main,
    checker: UpdateChecker? = nil
  ) {
    let info = bundle.infoDictionary
    let version = (info?["CFBundleShortVersionString"] as? String) ?? LogFile.engineVersion
    let repo = (info?["LogloomUpdateRepository"] as? String) ?? "GorkemSay/logloom"
    self.currentVersionString = version
    self.checker = checker ?? UpdateChecker(repository: repo)
  }

  var isChecking: Bool {
    if case .checking = state { return true }
    return false
  }

  var availableReleaseForBanner: ReleaseInfo? {
    guard !isBannerDismissed else { return nil }
    switch state {
    case .updateAvailable(_, let release),
      .downloading(let release, _, _, _),
      .readyToInstall(let release, _):
      return release
    case .idle, .checking, .upToDate, .noReleasesPublished, .failed:
      return nil
    }
  }

  func checkOnLaunchIfNeeded() {
    guard !hasCheckedOnLaunch else { return }
    hasCheckedOnLaunch = true
    Task { @MainActor [weak self] in
      try? await Task.sleep(for: .milliseconds(900))
      guard let self else { return }
      self.checkForUpdates(userInitiated: false)
    }
  }

  func checkForUpdates(userInitiated: Bool) {
    if userInitiated {
      isSheetPresented = true
      isBannerDismissed = false
    }

    if case .downloading = state {
      return
    }
    if case .readyToInstall = state, userInitiated {
      return
    }

    checkTask?.cancel()
    if userInitiated || state == .idle {
      state = .checking
    }

    let checker = self.checker
    let version = self.currentVersionString

    checkTask = Task { [weak self] in
      do {
        let outcome = try await checker.checkForUpdate(currentVersionString: version)
        guard !Task.isCancelled, let self else { return }
        self.applyOutcome(outcome, userInitiated: userInitiated)
      } catch is CancellationError {
        return
      } catch {
        guard !Task.isCancelled, let self else { return }
        if userInitiated {
          self.state = .failed(
            message: error.localizedDescription,
            releasesPageURL: checker.releasesPageURL
          )
        } else if case .checking = self.state {
          self.state = .idle
        }
      }
    }
  }

  private func applyOutcome(_ outcome: UpdateCheckOutcome, userInitiated: Bool) {
    switch outcome {
    case .upToDate(let current):
      if userInitiated {
        state = .upToDate(currentVersion: current.description)
      } else {
        state = .idle
      }
    case .noReleasesPublished(let current, let releasesURL):
      if userInitiated {
        state = .noReleasesPublished(
          currentVersion: current.description,
          releasesPageURL: releasesURL
        )
      } else {
        state = .idle
      }
    case .updateAvailable(let current, let release):
      let skipped = UserDefaults.standard.string(forKey: Self.skippedVersionKey)
      if !userInitiated && skipped == release.version.description {
        state = .idle
        return
      }
      isBannerDismissed = false
      state = .updateAvailable(currentVersion: current.description, release: release)
    }
  }

  func startUpdate(for release: ReleaseInfo) {
    guard let asset = release.preferredAsset else {
      openURL(release.htmlURL)
      return
    }

    downloadController?.cancel()
    state = .downloading(
      release: release,
      progress: 0,
      bytesWritten: 0,
      totalBytes: max(asset.sizeBytes, 0)
    )

    let controller = UpdateDownloadController(
      asset: asset,
      onProgress: { [weak self] written, total in
        guard let self else { return }
        let expectedTotal = total > 0 ? total : max(asset.sizeBytes, 0)
        let fraction = expectedTotal > 0 ? min(max(Double(written) / Double(expectedTotal), 0), 1) : 0
        self.state = .downloading(
          release: release,
          progress: fraction,
          bytesWritten: written,
          totalBytes: expectedTotal
        )
      },
      onCompletion: { [weak self] result in
        guard let self else { return }
        self.downloadController = nil
        switch result {
        case .success(let localURL):
          self.state = .readyToInstall(release: release, localURL: localURL)
          NSWorkspace.shared.open(localURL)
        case .failure(let error):
          let nsError = error as NSError
          if nsError.domain == NSURLErrorDomain && nsError.code == NSURLErrorCancelled {
            self.state = .updateAvailable(
              currentVersion: self.currentVersionString,
              release: release
            )
          } else {
            self.state = .failed(
              message: error.localizedDescription,
              releasesPageURL: release.htmlURL
            )
          }
        }
      }
    )
    downloadController = controller
    controller.start()
  }

  func cancelDownload() {
    downloadController?.cancel()
  }

  func openDownloadedInstaller(_ url: URL) {
    NSWorkspace.shared.open(url)
  }

  func revealDownloadedInstaller(_ url: URL) {
    NSWorkspace.shared.activateFileViewerSelecting([url])
  }

  func openURL(_ url: URL) {
    NSWorkspace.shared.open(url)
  }

  func skipVersion(_ version: String) {
    UserDefaults.standard.set(version, forKey: Self.skippedVersionKey)
    isBannerDismissed = true
    isSheetPresented = false
    state = .idle
  }
}

private final class UpdateDownloadController: NSObject, URLSessionDownloadDelegate, @unchecked Sendable {
  private let asset: ReleaseAsset
  private let onProgress: @MainActor (Int64, Int64) -> Void
  private let onCompletion: @MainActor (Result<URL, Error>) -> Void
  private var session: URLSession?
  private var task: URLSessionDownloadTask?

  init(
    asset: ReleaseAsset,
    onProgress: @escaping @MainActor (Int64, Int64) -> Void,
    onCompletion: @escaping @MainActor (Result<URL, Error>) -> Void
  ) {
    self.asset = asset
    self.onProgress = onProgress
    self.onCompletion = onCompletion
    super.init()
  }

  func start() {
    let config = URLSessionConfiguration.ephemeral
    let session = URLSession(configuration: config, delegate: self, delegateQueue: nil)
    self.session = session
    let request = URLRequest(
      url: asset.downloadURL,
      cachePolicy: .reloadIgnoringLocalCacheData,
      timeoutInterval: 120
    )
    let downloadTask = session.downloadTask(with: request)
    self.task = downloadTask
    downloadTask.resume()
  }

  func cancel() {
    task?.cancel()
  }

  func urlSession(
    _ session: URLSession,
    downloadTask: URLSessionDownloadTask,
    didWriteData bytesWritten: Int64,
    totalBytesWritten: Int64,
    totalBytesExpectedToWrite: Int64
  ) {
    Task { @MainActor [onProgress] in
      onProgress(totalBytesWritten, totalBytesExpectedToWrite)
    }
  }

  func urlSession(
    _ session: URLSession,
    downloadTask: URLSessionDownloadTask,
    didFinishDownloadingTo location: URL
  ) {
    let updatesDir = FileManager.default.temporaryDirectory
      .appendingPathComponent("LogloomUpdates", isDirectory: true)
    let safeName = URL(fileURLWithPath: asset.name).lastPathComponent
    let destinationURL = updatesDir.appendingPathComponent(safeName, isDirectory: false)

    do {
      try FileManager.default.createDirectory(at: updatesDir, withIntermediateDirectories: true)
      if FileManager.default.fileExists(atPath: destinationURL.path) {
        try FileManager.default.removeItem(at: destinationURL)
      }
      try FileManager.default.moveItem(at: location, to: destinationURL)
      session.finishTasksAndInvalidate()
      Task { @MainActor [onCompletion] in
        onCompletion(.success(destinationURL))
      }
    } catch {
      session.finishTasksAndInvalidate()
      Task { @MainActor [onCompletion] in
        onCompletion(.failure(error))
      }
    }
  }

  func urlSession(
    _ session: URLSession,
    task: URLSessionTask,
    didCompleteWithError error: Error?
  ) {
    guard let error else { return }
    session.finishTasksAndInvalidate()
    Task { @MainActor [onCompletion] in
      onCompletion(.failure(error))
    }
  }
}
