import LogloomCore
import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @ObservedObject var model: AppModel
    @State private var isDropTargeted = false
    @FocusState private var isSearchFieldFocused: Bool

    var body: some View {
        Group {
            if model.hasOpenFile {
                loadedView
            } else {
                emptyStateView
            }
        }
        .frame(minWidth: 760, minHeight: 480)
        .navigationTitle(model.fileName ?? "Logloom")
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

            Text("⌘O Open · ⌘F Search · ⌘L Jump to Line · ⌥⌘I Inspector")
                .font(.caption2)
                .foregroundStyle(.tertiary)
                .padding(.top, 4)

            Text("liblogloom \(LogFile.engineVersion)")
                .font(.caption2.monospaced())
                .foregroundStyle(.tertiary)
                .padding(.top, 2)
        }
        .padding(32)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var loadedView: some View {
        VStack(spacing: 0) {
            fileHeaderBar

            if model.isIndexing {
                ProgressView(value: model.indexProgress)
                    .progressViewStyle(.linear)
                    .controlSize(.small)
            } else {
                Divider()
            }

            if let errorMessage = model.errorMessage {
                bannerRow(text: errorMessage, color: .red)
            }

            if model.hasTruncatedFinalLine && !model.truncatedBannerDismissed {
                truncatedEOFBanner
            }

            SearchFilterBarView(model: model, isSearchFieldFocused: $isSearchFieldFocused)
            Divider()

            if let searchError = model.searchError {
                bannerRow(text: searchError, color: .red, icon: "exclamationmark.circle")
            }

            if let traceID = model.isolatedTraceID {
                TraceBannerView(
                    traceID: traceID,
                    eventCount: model.searchMatchCount,
                    onExit: { model.clearTraceIsolation() }
                )
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
            StatusBarView(model: model)
        }
        .onExitCommand {
            if model.isFilterActive {
                model.clearAllFilters()
            }
        }
    }

    private var fileHeaderBar: some View {
        HStack(spacing: 12) {
            Text(model.fileName ?? "")
                .font(.headline)
                .lineLimit(1)

            Text(model.formattedFileSize)
                .font(.caption.monospacedDigit())
                .foregroundStyle(.secondary)

            Spacer()

            if model.isIndexing {
                Text("Indexing \(Int((model.indexProgress * 100).rounded()))% (\(model.indexedLinesSoFar) lines)")
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
    }

    private var truncatedEOFBanner: some View {
        VStack(spacing: 0) {
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
            .padding(.horizontal, 14)
            .padding(.vertical, 5)
            Divider()
        }
    }

    private func handleDrop(providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first(where: {
            $0.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier)
        }) else {
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
