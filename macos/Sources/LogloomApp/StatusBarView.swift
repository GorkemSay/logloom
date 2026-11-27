import SwiftUI

struct StatusBarView: View {
    @ObservedObject var model: AppModel

    var body: some View {
        HStack(spacing: 12) {
            rowSummaryText

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

    @ViewBuilder
    private var rowSummaryText: some View {
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
    }
}
