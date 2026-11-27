import SwiftUI

struct StatusBarView: View {
  @ObservedObject var model: AppModel

  var body: some View {
    HStack(spacing: 10) {
      Circle()
        .fill(LogloomTheme.readOnlyDot)
        .frame(width: 6, height: 6)

      rowSummaryText

      if model.oversizedLineCount > 0 {
        Text("· \(model.oversizedLineCount) oversized skipped")
          .foregroundStyle(LogloomTheme.copper)
      }

      if let exportMsg = model.exportStatusMessage {
        Text("· \(exportMsg)")
          .foregroundStyle(LogloomTheme.textSoft)
          .lineLimit(1)
      }

      Spacer()

      if let selectedLine = model.selectedLineNumber {
        Text("Line \(selectedLine)")
          .foregroundStyle(LogloomTheme.textMuted)
      }
    }
    .font(.system(size: 11).monospacedDigit())
    .foregroundStyle(LogloomTheme.textMuted)
    .padding(.horizontal, 16)
    .padding(.vertical, 6)
    .background(LogloomTheme.panel)
  }

  @ViewBuilder
  private var rowSummaryText: some View {
    if model.isFilterActive {
      let suffix = model.searchLimitReached ? " (100k limit)" : ""
      Text("Read-only · \(model.rowCount) of \(model.totalFileLineCount) lines\(suffix)")
    } else if model.isIndexed {
      Text("Read-only · \(model.rowCount) lines")
    } else {
      Text("Read-only · Preview: \(model.rowCount) lines (indexing…)")
    }
  }
}
