import SwiftUI

struct SearchFilterBarView: View {
    @ObservedObject var model: AppModel
    var isSearchFieldFocused: FocusState<Bool>.Binding

    private static let levelOptions = ["ERROR", "WARN", "INFO", "DEBUG"]

    var body: some View {
        HStack(spacing: 8) {
            HStack(spacing: 6) {
                Image(systemName: "magnifyingglass")
                    .font(.caption)
                    .foregroundStyle(.secondary)

                TextField("Search logs...", text: $model.searchText)
                    .textFieldStyle(.plain)
                    .font(.system(.callout, design: .monospaced))
                    .focused(isSearchFieldFocused)
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
}

struct TraceBannerView: View {
    let traceID: String
    let eventCount: Int
    let onExit: () -> Void

    var body: some View {
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

            Text("\(eventCount) event(s)")
                .font(.caption.monospacedDigit())
                .foregroundStyle(.secondary)

            Spacer()

            Button("Exit Trace", action: onExit)
                .font(.caption)
                .buttonStyle(.borderless)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 6)
        .background(Color.accentColor.opacity(0.08))
    }
}
