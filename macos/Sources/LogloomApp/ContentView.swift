import LogloomCore
import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
  @ObservedObject var model: AppModel
  @State private var isDropTargeted = false
  @FocusState private var isSearchFieldFocused: Bool

  private static let levelOptions = ["ERROR", "WARN", "INFO", "DEBUG"]

  var body: some View {
    Group {
      if model.hasOpenFile {
        loadedView
      } else {
        emptyStateView
      }
    }
    .frame(minWidth: 760, minHeight: 480)
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
    .onDrop(of: [UTType.fileURL], isTargeted: $isDropTargeted, perform: handleDrop(providers:))
    .overlay {
      if isDropTargeted {
        RoundedRectangle(cornerRadius: 8)
          .strokeBorder(Color.accentColor, lineWidth: 2)
          .padding(6)
          .allowsHitTesting(false)
      }
    }
    .toolbar {
      ToolbarItemGroup(placement: .primaryAction) {
        Button("Open...") {
          model.openPanel()
        }
        if model.hasOpenFile {
          Button("Export Filtered...") {
            model.exportFilteredPanel()
          }
          .disabled(!model.isFilterActive || model.isExporting)

          Button {
            model.toggleInspector()
          } label: {
            Image(systemName: "sidebar.right")
          }
          .help(model.isInspectorVisible ? "Hide Line Inspector" : "Show Line Inspector")

          Button("Close") {
            model.closeFile()
          }
        }
      }
    }
  }

  private var emptyStateView: some View {
    VStack(spacing: 12) {
      Text("Logloom")
        .font(.title2.weight(.semibold))

      Text("Drop a .jsonl or .log file here, or choose one to inspect read-only.")
        .font(.callout)
        .foregroundStyle(.secondary)

      Button("Open Log File...") {
        model.openPanel()
      }
      .buttonStyle(.borderedProminent)
      .keyboardShortcut("o", modifiers: .command)

      if let errorMessage = model.errorMessage {
        Text(errorMessage)
          .font(.caption)
          .foregroundStyle(.red)
          .padding(.top, 4)
      }

      Text("liblogloom \(LogFile.engineVersion)")
        .font(.caption2.monospaced())
        .foregroundStyle(.tertiary)
        .padding(.top, 8)
    }
    .padding(32)
    .frame(maxWidth: .infinity, maxHeight: .infinity)
  }

  private var loadedView: some View {
    VStack(spacing: 0) {
      HStack(spacing: 12) {
        Text(model.fileName ?? "")
          .font(.headline)
          .lineLimit(1)

        Text(model.formattedFileSize)
          .font(.caption.monospacedDigit())
          .foregroundStyle(.secondary)

        Spacer()

        if model.isIndexing {
          Text(
            "Indexing \(Int((model.indexProgress * 100).rounded()))% (\(model.indexedLinesSoFar) lines)"
          )
          .font(.caption.monospacedDigit())
          .foregroundStyle(.secondary)
        } else {
          Text("\(model.rowCount) line(s)")
            .font(.caption.monospacedDigit())
            .foregroundStyle(.secondary)
        }
      }
      .padding(.horizontal, 14)
      .padding(.vertical, 8)
      .background(.bar)

      if model.isIndexing {
        ProgressView(value: model.indexProgress)
          .progressViewStyle(.linear)
          .controlSize(.small)
      } else {
        Divider()
      }

      if let errorMessage = model.errorMessage {
        HStack(spacing: 8) {
          Text(errorMessage)
            .font(.caption)
            .foregroundStyle(.red)
          Spacer()
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 6)
        Divider()
      }

      if model.hasTruncatedFinalLine && !model.truncatedBannerDismissed {
        HStack(spacing: 8) {
          Image(systemName: "exclamationmark.triangle")
            .font(.caption)
            .foregroundStyle(.orange)
          Text("Final line has no trailing newline (truncated EOF).")
            .font(.caption)
            .foregroundStyle(.primary)
          Spacer()
          Button("Dismiss") {
            model.truncatedBannerDismissed = true
          }
          .font(.caption)
          .buttonStyle(.borderless)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 6)
        .background(Color.orange.opacity(0.10))
        Divider()
      }

      searchAndFilterBar
      Divider()

      if let searchError = model.searchError {
        HStack(spacing: 8) {
          Image(systemName: "exclamationmark.circle")
            .font(.caption)
            .foregroundStyle(.red)
          Text(searchError)
            .font(.caption)
            .foregroundStyle(.red)
          Spacer()
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 5)
        Divider()
      }

      if let traceID = model.isolatedTraceID {
        HStack(spacing: 8) {
          Image(systemName: "point.3.connected.trianglepath.dotted")
            .font(.caption)
            .foregroundStyle(Color.accentColor)
          Text("Trace")
            .font(.caption.weight(.medium))
            .foregroundStyle(.secondary)
          Text(traceID)
            .font(.system(.caption, design: .monospaced).weight(.medium))
            .textSelection(.enabled)
            .lineLimit(1)

          Text("\(model.searchMatchCount) event(s)")
            .font(.caption.monospacedDigit())
            .foregroundStyle(.secondary)

          Spacer()

          Button("Exit Trace") {
            model.clearTraceIsolation()
          }
          .font(.caption)
          .buttonStyle(.borderless)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 6)
        .background(Color.accentColor.opacity(0.08))
        Divider()
      }

      HSplitView {
        VirtualizedLogTableView(
          viewportStore: model.viewportStore,
          viewportRevision: model.viewportRevision,
          isolatedTraceID: model.isolatedTraceID,
          selectedLineNumber: $model.selectedLineNumber,
          scrollToSelectionTrigger: model.scrollToSelectionTrigger,
          onTraceIDClick: { model.isolateTrace($0) },
          onClearTraceIsolation: { model.clearTraceIsolation() }
        )
        .frame(minWidth: 420, maxWidth: .infinity, maxHeight: .infinity)

        if model.isInspectorVisible {
          LineInspectorView(model: model)
            .frame(minWidth: 280, idealWidth: 380, maxWidth: 640, maxHeight: .infinity)
        }
      }
      .frame(maxWidth: .infinity, maxHeight: .infinity)

      Divider()

      HStack(spacing: 12) {
        if model.isFilterActive {
          if model.searchLimitReached {
            Text("Showing \(model.rowCount) matching line(s) (100k limit reached)")
          } else {
            Text("Showing \(model.rowCount) matching line(s)")
          }
        } else if model.isIndexed {
          Text("\(model.rowCount) lines")
        } else {
          Text("Preview: \(model.rowCount) lines (indexing in background)")
        }

        if model.oversizedLineCount > 0 {
          Text("\(model.oversizedLineCount) oversized line(s) skipped")
            .foregroundStyle(.orange)
        }

        if let exportMsg = model.exportStatusMessage {
          Text(exportMsg)
            .foregroundStyle(.secondary)
            .lineLimit(1)
        }

        Spacer()

        if let selectedLine = model.selectedLineNumber {
          Text("Line \(selectedLine)")
        }
      }
      .font(.caption.monospacedDigit())
      .foregroundStyle(.secondary)
      .padding(.horizontal, 14)
      .padding(.vertical, 5)
      .background(.bar)
    }
    .onExitCommand {
      if model.isFilterActive {
        model.clearAllFilters()
      }
    }
  }

  private var searchAndFilterBar: some View {
    HStack(spacing: 8) {
      HStack(spacing: 6) {
        Image(systemName: "magnifyingglass")
          .font(.caption)
          .foregroundStyle(.secondary)

        TextField("Search logs...", text: $model.searchText)
          .textFieldStyle(.plain)
          .font(.system(.callout, design: .monospaced))
          .focused($isSearchFieldFocused)
          .onSubmit {
            model.scheduleSearch(immediate: true)
          }
          .onExitCommand {
            if model.isFilterActive {
              model.clearAllFilters()
            }
          }

        if !model.searchText.isEmpty {
          Button {
            model.searchText = ""
          } label: {
            Image(systemName: "xmark.circle.fill")
              .font(.caption)
              .foregroundStyle(.secondary)
          }
          .buttonStyle(.plain)
          .help("Clear search text")
        }
      }
      .padding(.horizontal, 8)
      .padding(.vertical, 4)
      .background(Color(nsColor: .textBackgroundColor))
      .overlay {
        RoundedRectangle(cornerRadius: 6)
          .stroke(Color(nsColor: .separatorColor), lineWidth: 1)
      }

      Toggle(isOn: $model.isCaseSensitive) {
        Text("Aa")
          .font(.system(.caption, design: .monospaced).weight(.medium))
      }
      .toggleStyle(.button)
      .controlSize(.small)
      .help("Case sensitive")

      Toggle(isOn: $model.isRegex) {
        Text(".*")
          .font(.system(.caption, design: .monospaced).weight(.medium))
      }
      .toggleStyle(.button)
      .controlSize(.small)
      .help("Regular expression")

      Picker("Level", selection: $model.selectedLevel) {
        Text("All Levels").tag(String?.none)
        ForEach(Self.levelOptions, id: \.self) { level in
          Text(level).tag(Optional(level))
        }
      }
      .labelsHidden()
      .pickerStyle(.menu)
      .controlSize(.small)
      .frame(width: 96)

      if model.isSearching {
        ProgressView()
          .controlSize(.small)
      }

      if model.isFilterActive {
        Text(filterStatusSummary)
          .font(.caption.monospacedDigit())
          .foregroundStyle(.secondary)
          .lineLimit(1)

        Button("Clear") {
          model.clearAllFilters()
        }
        .font(.caption)
        .buttonStyle(.borderless)
      }
    }
    .padding(.horizontal, 14)
    .padding(.vertical, 6)
    .background(.bar)
  }

  private var filterStatusSummary: String {
    var base = "\(model.searchMatchCount) match(es)"
    if model.searchLimitReached {
      base += " (limit)"
    }
    if !model.isSearching, let ms = model.searchDurationMs {
      base += String(format: " in %.0f ms", ms)
    }
    return base
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

private struct JumpToLineSheet: View {
  @ObservedObject var model: AppModel
  @Environment(\.dismiss) private var dismiss
  @State private var lineInput: String = ""
  @FocusState private var isInputFocused: Bool

  private var parsedLineNumber: UInt64? {
    let trimmed = lineInput.trimmingCharacters(in: .whitespacesAndNewlines)
    guard let value = UInt64(trimmed), value > 0 else {
      return nil
    }
    return value
  }

  var body: some View {
    VStack(alignment: .leading, spacing: 12) {
      Text("Jump to Line")
        .font(.headline)

      TextField("Line number (1...)", text: $lineInput)
        .textFieldStyle(.roundedBorder)
        .font(.system(.body, design: .monospaced).monospacedDigit())
        .focused($isInputFocused)
        .onSubmit(submit)

      HStack {
        Spacer()
        Button("Cancel") {
          dismiss()
        }
        .keyboardShortcut(.cancelAction)

        Button("Jump") {
          submit()
        }
        .keyboardShortcut(.defaultAction)
        .disabled(parsedLineNumber == nil)
      }
    }
    .padding(18)
    .frame(width: 280)
    .onAppear {
      if let selected = model.selectedLineNumber {
        lineInput = "\(selected)"
      }
      isInputFocused = true
    }
  }

  private func submit() {
    guard let target = parsedLineNumber else { return }
    model.jumpToLine(target, clearFiltersIfNeeded: true)
    dismiss()
  }
}
