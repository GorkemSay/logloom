import SwiftUI

struct SearchFilterBarView: View {
  @ObservedObject var model: AppModel
  var isSearchFieldFocused: FocusState<Bool>.Binding

  private static let levelOptions = ["DEBUG", "INFO", "WARN", "ERROR"]

  var body: some View {
    HStack(spacing: 9) {
      HStack(spacing: 6) {
        Image(systemName: "magnifyingglass")
          .font(.caption)
          .foregroundStyle(LogloomTheme.textMuted)

        TextField("Search logs...", text: $model.searchText)
          .textFieldStyle(.plain)
          .font(.system(.callout, design: .monospaced))
          .foregroundStyle(LogloomTheme.textPrimary)
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
              .foregroundStyle(LogloomTheme.textMuted)
          }
          .buttonStyle(.plain)
          .help("Clear search text")
        }

        inlineModeToggle(title: "Aa", isOn: $model.isCaseSensitive, help: "Case sensitive")
        inlineModeToggle(title: ".*", isOn: $model.isRegex, help: "Regular expression")
      }
      .padding(.horizontal, 10)
      .padding(.vertical, 4)
      .frame(minHeight: 31)
      .background(LogloomTheme.inputBg)
      .overlay {
        RoundedRectangle(cornerRadius: 6)
          .stroke(
            isSearchFieldFocused.wrappedValue
              ? LogloomTheme.inputFocusedBorder
              : LogloomTheme.inputBorder,
            lineWidth: 1
          )
      }

      if model.hasStructuredFields {
        Picker("Level", selection: $model.selectedLevel) {
          Text("All levels").tag(String?.none)
          ForEach(Self.levelOptions, id: \.self) { level in
            Text(level).tag(Optional(level))
          }
        }
        .labelsHidden()
        .pickerStyle(.menu)
        .controlSize(.small)
        .frame(width: 98)
      }

      if model.isSearching {
        ProgressView()
          .controlSize(.small)
      }

      if model.isFilterActive {
        Text(filterStatusSummary)
          .font(.caption.monospacedDigit())
          .foregroundStyle(LogloomTheme.textMuted)
          .lineLimit(1)

        Button("Clear") {
          model.clearAllFilters()
        }
        .font(.caption)
        .foregroundStyle(LogloomTheme.copper)
        .buttonStyle(.borderless)
      }
    }
    .padding(.horizontal, 16)
    .padding(.vertical, 10)
    .background(LogloomTheme.panel)
  }

  private func inlineModeToggle(title: String, isOn: Binding<Bool>, help: String) -> some View {
    Button {
      isOn.wrappedValue.toggle()
    } label: {
      Text(title)
        .font(.system(size: 11.5, design: .monospaced))
        .foregroundStyle(isOn.wrappedValue ? LogloomTheme.copper : LogloomTheme.textMuted)
        .padding(.horizontal, 6)
        .padding(.vertical, 3)
        .background(
          RoundedRectangle(cornerRadius: 4)
            .fill(isOn.wrappedValue ? LogloomTheme.toggleOnBg : Color.clear)
        )
    }
    .buttonStyle(.plain)
    .help(help)
    .accessibilityAddTraits(isOn.wrappedValue ? .isSelected : [])
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
      Text("Trace")
        .font(.caption)
        .foregroundStyle(LogloomTheme.traceStripText)
      Text(traceID)
        .font(.system(.caption, design: .monospaced).weight(.semibold))
        .foregroundStyle(LogloomTheme.traceStripText)
        .textSelection(.enabled)
        .lineLimit(1)

      Text("· \(eventCount) event(s)")
        .font(.caption.monospacedDigit())
        .foregroundStyle(LogloomTheme.traceStripText)

      Spacer()

      Button("Exit trace ×", action: onExit)
        .font(.caption.weight(.medium))
        .foregroundStyle(LogloomTheme.copper)
        .buttonStyle(.plain)
    }
    .padding(.horizontal, 18)
    .padding(.vertical, 7)
    .background(LogloomTheme.traceStripBg)
    .overlay(alignment: .bottom) {
      Rectangle()
        .fill(LogloomTheme.traceStripBorder)
        .frame(height: 1)
    }
  }
}
