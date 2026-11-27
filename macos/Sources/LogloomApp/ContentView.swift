import AppKit
import LogloomCore
import SwiftUI
import UniformTypeIdentifiers

private struct WindowTitleSyncView: NSViewRepresentable {
  let title: String

  func makeNSView(context: Context) -> NSView {
    let view = NSView(frame: .zero)
    DispatchQueue.main.async {
      configure(window: view.window)
    }
    return view
  }

  func updateNSView(_ nsView: NSView, context: Context) {
    DispatchQueue.main.async {
      configure(window: nsView.window)
    }
  }

  private func configure(window: NSWindow?) {
    guard let window else { return }
    if window.title != title {
      window.title = title
    }
    window.titleVisibility = .hidden
    window.titlebarAppearsTransparent = true
  }
}

struct ContentView: View {
  @ObservedObject var model: AppModel
  @ObservedObject var updateManager: UpdateManager
  @State private var isDropTargeted = false
  @FocusState private var isSearchFieldFocused: Bool

  var body: some View {
    VStack(spacing: 0) {
      topChromeBar

      if updateManager.availableReleaseForBanner != nil {
        UpdateBannerView(updateManager: updateManager)
      }

      ZStack {
        if model.hasOpenFile {
          loadedView
        } else {
          emptyStateView
        }
      }
      .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
    .ignoresSafeArea(.container, edges: .top)
    .background(LogloomTheme.canvas)
    .background(
      WindowTitleSyncView(title: model.fileName.map { "Logloom — \($0)" } ?? "Logloom")
    )
    .frame(minWidth: 700, minHeight: 460)
    .onChange(of: model.focusSearchTrigger) { _, _ in
      isSearchFieldFocused = true
    }
    .onChange(of: model.searchText) { _, _ in
      model.scheduleSearch(immediate: false)
    }
    .onChange(of: model.isRegex) { _, _ in
      model.scheduleSearch(immediate: true)
    }
    .onChange(of: model.isCaseSensitive) { _, _ in
      model.scheduleSearch(immediate: true)
    }
    .onChange(of: model.selectedLevel) { _, _ in
      model.scheduleSearch(immediate: true)
    }
    .onChange(of: model.selectedLineNumber) { _, _ in
      model.refreshSelectedLineAndContext()
    }
    .onChange(of: model.contextRadius) { _, _ in
      model.refreshSelectedLineAndContext()
    }
    .onChange(of: model.isInspectorVisible) { _, _ in
      model.refreshSelectedLineAndContext()
    }
    .sheet(isPresented: $model.isJumpToLinePresented) {
      JumpToLineSheet(model: model)
    }
    .sheet(isPresented: $updateManager.isSheetPresented) {
      UpdateSheetView(updateManager: updateManager)
    }
    .onDrop(of: [UTType.fileURL], isTargeted: $isDropTargeted, perform: handleDrop(providers:))
    .overlay {
      if isDropTargeted {
        RoundedRectangle(cornerRadius: 8)
          .strokeBorder(LogloomTheme.copper, lineWidth: 2)
          .padding(6)
          .allowsHitTesting(false)
      }
    }
  }

  private var topChromeBar: some View {
    HStack(spacing: 10) {
      LogloomMarkView(size: 18, cornerRadius: 4)

      Text("Logloom")
        .font(.system(size: 13, weight: .semibold))
        .foregroundStyle(LogloomTheme.textPrimary)

      if model.hasOpenFile {
        Text("/")
          .font(.system(size: 13, weight: .regular))
          .foregroundStyle(LogloomTheme.textMuted)

        Text(model.fileName ?? "")
          .font(.system(size: 13, weight: .medium))
          .foregroundStyle(LogloomTheme.textPrimary)
          .lineLimit(1)
          .truncationMode(.middle)
      }

      Spacer(minLength: 12)

      HStack(spacing: 12) {
        Button {
          model.openPanel()
        } label: {
          HStack(spacing: 5) {
            Image(systemName: "folder")
              .font(.system(size: 12))
            Text("Open...")
              .font(.system(size: 12.5, weight: .medium))
          }
          .foregroundStyle(LogloomTheme.textSoft)
          .padding(.horizontal, 6)
          .padding(.vertical, 4)
          .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .help("Open Log File (⌘O)")

        if model.hasOpenFile {
          Button {
            model.exportFilteredPanel()
          } label: {
            HStack(spacing: 5) {
              Image(systemName: "square.and.arrow.up")
                .font(.system(size: 12))
              Text("Export...")
                .font(.system(size: 12.5, weight: .medium))
            }
            .foregroundStyle(
              (!model.isFilterActive || model.isExporting)
                ? LogloomTheme.textMuted.opacity(0.55)
                : LogloomTheme.textSoft
            )
            .padding(.horizontal, 6)
            .padding(.vertical, 4)
            .contentShape(Rectangle())
          }
          .buttonStyle(.plain)
          .disabled(!model.isFilterActive || model.isExporting)
          .help("Export Filtered Results (⇧⌘E)")

          Button {
            model.toggleInspector()
          } label: {
            Image(systemName: "sidebar.right")
              .font(.system(size: 13))
              .foregroundStyle(
                model.isInspectorVisible ? LogloomTheme.copper : LogloomTheme.textSoft
              )
              .padding(5)
              .contentShape(Rectangle())
          }
          .buttonStyle(.plain)
          .help(model.isInspectorVisible ? "Hide Inspector (⌥⌘I)" : "Show Inspector (⌥⌘I)")

          Button {
            model.closeFile()
          } label: {
            Image(systemName: "xmark")
              .font(.system(size: 12, weight: .medium))
              .foregroundStyle(LogloomTheme.textSoft)
              .padding(5)
              .contentShape(Rectangle())
          }
          .buttonStyle(.plain)
          .help("Close File")
        }
      }
    }
    .padding(.leading, 78)
    .padding(.trailing, 14)
    .frame(height: 36)
    .frame(maxWidth: .infinity)
    .background(LogloomTheme.panel)
    .overlay(alignment: .bottom) {
      Rectangle()
        .fill(LogloomTheme.border)
        .frame(height: 1)
    }
  }

  private var emptyStateView: some View {
    VStack(spacing: 14) {
      LogloomMarkView(size: 72, cornerRadius: 16)
        .padding(.bottom, 4)

      Text("Open a log file")
        .font(.system(size: 22, weight: .semibold))
        .foregroundStyle(LogloomTheme.textPrimary)

      Text("Drop a .log or .jsonl file here to inspect it locally.")
        .font(.callout)
        .foregroundStyle(LogloomTheme.textSoft)

      Button {
        model.openPanel()
      } label: {
        Text("Choose file…")
          .font(.system(size: 13, weight: .semibold))
          .foregroundStyle(LogloomTheme.accentButtonFg)
          .padding(.horizontal, 16)
          .padding(.vertical, 8)
          .background(
            RoundedRectangle(cornerRadius: 6, style: .continuous)
              .fill(LogloomTheme.accentButtonBg)
          )
      }
      .buttonStyle(.plain)
      .keyboardShortcut("o", modifiers: .command)
      .padding(.top, 4)

      if let errorMessage = model.errorMessage {
        Text(errorMessage)
          .font(.caption)
          .foregroundStyle(LogloomTheme.levelColor(for: "ERROR"))
          .padding(.top, 4)
      }

      Text("⌘O Open · ⌘F Search · ⌘L Jump to Line · ⌥⌘I Inspector")
        .font(.caption2)
        .foregroundStyle(LogloomTheme.textMuted)
        .padding(.top, 8)

      Text("liblogloom \(LogFile.engineVersion)")
        .font(.caption2.monospaced())
        .foregroundStyle(LogloomTheme.textMuted)
    }
    .padding(32)
    .frame(maxWidth: .infinity, maxHeight: .infinity)
    .background(LogloomTheme.canvas)
  }

  private var loadedView: some View {
    VStack(spacing: 0) {
      SearchFilterBarView(model: model, isSearchFieldFocused: $isSearchFieldFocused)

      if model.isIndexing {
        ProgressView(value: model.indexProgress)
          .progressViewStyle(.linear)
          .controlSize(.small)
          .tint(LogloomTheme.copper)
      } else {
        Divider().overlay(LogloomTheme.border)
      }

      if let errorMessage = model.errorMessage {
        bannerRow(text: errorMessage, color: LogloomTheme.levelColor(for: "ERROR"))
      }

      if model.hasTruncatedFinalLine && !model.truncatedBannerDismissed {
        truncatedEOFBanner
      }

      if let searchError = model.searchError {
        bannerRow(
          text: searchError,
          color: LogloomTheme.levelColor(for: "ERROR"),
          icon: "exclamationmark.circle"
        )
      }

      if let traceID = model.isolatedTraceID {
        TraceBannerView(
          traceID: traceID,
          eventCount: model.searchMatchCount,
          onExit: { model.clearTraceIsolation() }
        )
      }

      HSplitView {
        VStack(spacing: 0) {
          sectionHeaderBar

          VirtualizedLogTableView(
            viewportStore: model.viewportStore,
            viewportRevision: model.viewportRevision,
            isStructuredMode: model.hasStructuredFields,
            isolatedTraceID: model.isolatedTraceID,
            selectedLineNumber: $model.selectedLineNumber,
            scrollToSelectionTrigger: model.scrollToSelectionTrigger,
            onTraceIDClick: { model.isolateTrace($0) },
            onClearTraceIsolation: { model.clearTraceIsolation() },
            onObserveStructured: { model.markStructuredFieldsDetected() }
          )
          .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .frame(minWidth: 360, maxWidth: .infinity, maxHeight: .infinity)
        .background(LogloomTheme.canvas)

        if model.isInspectorVisible, model.selectedLine != nil {
          LineInspectorView(model: model)
            .frame(minWidth: 290, idealWidth: 360, maxWidth: 520, maxHeight: .infinity)
        }
      }
      .frame(maxWidth: .infinity, maxHeight: .infinity)

      Divider().overlay(LogloomTheme.border)
      StatusBarView(model: model)
    }
    .onExitCommand {
      if model.isFilterActive {
        model.clearAllFilters()
      }
    }
  }

  private var sectionHeaderBar: some View {
    HStack {
      Text("Log entries")
        .font(.system(size: 13, weight: .semibold))
        .foregroundStyle(LogloomTheme.textPrimary)

      Spacer()

      if model.isIndexing {
        Text(
          "Indexing \(Int((model.indexProgress * 100).rounded()))% (\(model.indexedLinesSoFar)) · \(model.formattedFileSize)"
        )
        .font(.system(size: 11).monospacedDigit())
        .foregroundStyle(LogloomTheme.textMuted)
      } else {
        Text(model.formattedFileSize)
          .font(.system(size: 11).monospacedDigit())
          .foregroundStyle(LogloomTheme.textMuted)
      }
    }
    .padding(.horizontal, 16)
    .padding(.vertical, 10)
    .background(LogloomTheme.canvas)
  }

  private var truncatedEOFBanner: some View {
    HStack(spacing: 8) {
      Image(systemName: "exclamationmark.triangle")
        .font(.caption)
        .foregroundStyle(LogloomTheme.copper)
      Text("Final line has no trailing newline (truncated EOF).")
        .font(.caption)
        .foregroundStyle(LogloomTheme.traceStripText)
      Spacer()
      Button("Dismiss") {
        model.truncatedBannerDismissed = true
      }
      .font(.caption)
      .foregroundStyle(LogloomTheme.copper)
      .buttonStyle(.borderless)
    }
    .padding(.horizontal, 16)
    .padding(.vertical, 7)
    .background(LogloomTheme.traceStripBg)
    .overlay(alignment: .bottom) {
      Rectangle().fill(LogloomTheme.traceStripBorder).frame(height: 1)
    }
  }

  private func bannerRow(text: String, color: Color, icon: String? = nil) -> some View {
    VStack(spacing: 0) {
      HStack(spacing: 8) {
        if let icon {
          Image(systemName: icon)
            .font(.caption)
            .foregroundStyle(color)
        }
        Text(text)
          .font(.caption)
          .foregroundStyle(color)
        Spacer()
      }
      .padding(.horizontal, 16)
      .padding(.vertical, 6)
      Divider().overlay(LogloomTheme.border)
    }
  }

  private func handleDrop(providers: [NSItemProvider]) -> Bool {
    guard
      let provider = providers.first(where: {
        $0.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier)
      })
    else {
      return false
    }

    provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
      let resolvedURL: URL?
      if let data = item as? Data {
        resolvedURL = URL(dataRepresentation: data, relativeTo: nil)
      } else if let url = item as? URL {
        resolvedURL = url
      } else {
        resolvedURL = nil
      }
      guard let fileURL = resolvedURL else {
        return
      }
      Task { @MainActor in
        model.openFile(url: fileURL)
      }
    }
    return true
  }
}
