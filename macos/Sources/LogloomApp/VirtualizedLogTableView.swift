import AppKit
import LogloomCore
import SwiftUI

struct VirtualizedLogTableView: NSViewRepresentable {
    let lineCache: LinePageCache
    let viewportRevision: UInt64
    @Binding var selectedLineNumber: UInt64?

    func makeCoordinator() -> Coordinator {
        Coordinator(lineCache: lineCache, selectedLineNumber: $selectedLineNumber)
    }

    func makeNSView(context: Context) -> NSScrollView {
        let scrollView = NSScrollView()
        scrollView.hasVerticalScroller = true
        scrollView.hasHorizontalScroller = true
        scrollView.autohidesScrollers = true
        scrollView.borderType = .noBorder
        scrollView.drawsBackground = true
        scrollView.backgroundColor = .textBackgroundColor

        let tableView = NSTableView()
        tableView.style = .plain
        tableView.usesAlternatingRowBackgroundColors = false
        tableView.rowHeight = 20
        tableView.usesAutomaticRowHeights = false
        tableView.intercellSpacing = NSSize(width: 8, height: 0)
        tableView.allowsMultipleSelection = false
        tableView.allowsEmptySelection = true
        tableView.allowsColumnSelection = false
        tableView.allowsColumnReordering = false
        tableView.columnAutoresizingStyle = .lastColumnOnlyAutoresizingStyle
        tableView.gridStyleMask = []
        tableView.headerView = NSTableHeaderView()

        for spec in ColumnSpec.allCases {
            let column = NSTableColumn(identifier: spec.identifier)
            column.title = spec.title
            column.width = spec.defaultWidth
            column.minWidth = spec.minWidth
            column.maxWidth = spec.maxWidth
            column.resizingMask = spec.resizingMask
            column.headerCell.alignment = spec.alignment
            tableView.addTableColumn(column)
        }

        tableView.delegate = context.coordinator
        tableView.dataSource = context.coordinator

        scrollView.documentView = tableView
        context.coordinator.tableView = tableView
        context.coordinator.scrollView = scrollView
        context.coordinator.lastRevision = viewportRevision

        return scrollView
    }

    func updateNSView(_ scrollView: NSScrollView, context: Context) {
        let coordinator = context.coordinator
        coordinator.selectedLineNumberBinding = $selectedLineNumber
        coordinator.lineCache = lineCache

        guard let tableView = coordinator.tableView else { return }

        if coordinator.lastRevision != viewportRevision {
            coordinator.lastRevision = viewportRevision
            let previousOrigin = scrollView.contentView.bounds.origin
            let savedSelection = selectedLineNumber

            coordinator.isProgrammaticUpdate = true
            tableView.reloadData()

            if let savedSelection, savedSelection > 0 {
                let targetRow = Int(clamping: savedSelection - 1)
                if targetRow >= 0 && targetRow < lineCache.rowCount {
                    tableView.selectRowIndexes(IndexSet(integer: targetRow), byExtendingSelection: false)
                }
            }

            if previousOrigin.y > 0 || previousOrigin.x > 0 {
                let contentHeight = tableView.bounds.height
                let clipHeight = scrollView.contentView.bounds.height
                let maxY = max(0, contentHeight - clipHeight)
                let clampedOrigin = NSPoint(x: previousOrigin.x, y: min(previousOrigin.y, maxY))
                scrollView.contentView.scroll(to: clampedOrigin)
                scrollView.reflectScrolledClipView(scrollView.contentView)
            }

            coordinator.isProgrammaticUpdate = false
        } else {
            syncExternalSelection(tableView: tableView, coordinator: coordinator)
        }
    }

    private func syncExternalSelection(tableView: NSTableView, coordinator: Coordinator) {
        let desiredRow: Int?
        if let selectedLineNumber, selectedLineNumber > 0 {
            let row = Int(clamping: selectedLineNumber - 1)
            desiredRow = row < lineCache.rowCount ? row : nil
        } else {
            desiredRow = nil
        }

        let currentRow = tableView.selectedRow >= 0 ? tableView.selectedRow : nil
        guard currentRow != desiredRow else { return }

        coordinator.isProgrammaticUpdate = true
        if let desiredRow {
            tableView.selectRowIndexes(IndexSet(integer: desiredRow), byExtendingSelection: false)
        } else {
            tableView.deselectAll(nil)
        }
        coordinator.isProgrammaticUpdate = false
    }

    enum ColumnSpec: CaseIterable {
        case lineNumber
        case timestamp
        case level
        case traceID
        case content

        var identifier: NSUserInterfaceItemIdentifier {
            switch self {
            case .lineNumber: return NSUserInterfaceItemIdentifier("lineNumber")
            case .timestamp: return NSUserInterfaceItemIdentifier("timestamp")
            case .level: return NSUserInterfaceItemIdentifier("level")
            case .traceID: return NSUserInterfaceItemIdentifier("traceID")
            case .content: return NSUserInterfaceItemIdentifier("content")
            }
        }

        var title: String {
            switch self {
            case .lineNumber: return "#"
            case .timestamp: return "Timestamp"
            case .level: return "Level"
            case .traceID: return "Trace ID"
            case .content: return "Content"
            }
        }

        var defaultWidth: CGFloat {
            switch self {
            case .lineNumber: return 68
            case .timestamp: return 170
            case .level: return 62
            case .traceID: return 145
            case .content: return 540
            }
        }

        var minWidth: CGFloat {
            switch self {
            case .lineNumber: return 48
            case .timestamp: return 110
            case .level: return 50
            case .traceID: return 90
            case .content: return 220
            }
        }

        var maxWidth: CGFloat {
            switch self {
            case .lineNumber: return 110
            case .timestamp: return 280
            case .level: return 96
            case .traceID: return 320
            case .content: return 10_000
            }
        }

        var alignment: NSTextAlignment {
            switch self {
            case .lineNumber: return .right
            default: return .left
            }
        }

        var resizingMask: NSTableColumn.ResizingOptions {
            switch self {
            case .content: return [.autoresizingMask, .userResizingMask]
            default: return .userResizingMask
            }
        }

        init?(identifier: NSUserInterfaceItemIdentifier?) {
            guard let identifier else { return nil }
            guard let match = ColumnSpec.allCases.first(where: { $0.identifier == identifier }) else {
                return nil
            }
            self = match
        }
    }

    @MainActor
    final class Coordinator: NSObject, NSTableViewDataSource, NSTableViewDelegate {
        var lineCache: LinePageCache
        var selectedLineNumberBinding: Binding<UInt64?>
        weak var tableView: NSTableView?
        weak var scrollView: NSScrollView?
        var lastRevision: UInt64 = 0
        var isProgrammaticUpdate = false

        private let lineNumberFont = NSFont.monospacedDigitSystemFont(ofSize: 11.5, weight: .regular)
        private let monoRegularFont = NSFont.monospacedSystemFont(ofSize: 12, weight: .regular)
        private let monoMediumFont = NSFont.monospacedSystemFont(ofSize: 11, weight: .medium)

        init(lineCache: LinePageCache, selectedLineNumber: Binding<UInt64?>) {
            self.lineCache = lineCache
            self.selectedLineNumberBinding = selectedLineNumber
        }

        func numberOfRows(in tableView: NSTableView) -> Int {
            lineCache.rowCount
        }

        func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
            guard let columnSpec = ColumnSpec(identifier: tableColumn?.identifier) else {
                return nil
            }

            let cellView = reusableCellView(in: tableView, columnSpec: columnSpec)
            guard let textField = cellView.textField else {
                return cellView
            }

            guard let line = lineCache.line(atRow: row) else {
                textField.stringValue = ""
                return cellView
            }

            switch columnSpec {
            case .lineNumber:
                textField.font = lineNumberFont
                textField.textColor = .secondaryLabelColor
                textField.stringValue = "\(line.lineNumber)"

            case .timestamp:
                textField.font = lineNumberFont
                textField.textColor = .secondaryLabelColor
                textField.stringValue = line.fields.timestamp ?? ""

            case .level:
                textField.font = monoMediumFont
                let levelText = line.fields.level ?? ""
                textField.stringValue = levelText
                textField.textColor = levelColor(for: levelText)

            case .traceID:
                textField.font = monoRegularFont
                textField.textColor = .secondaryLabelColor
                textField.stringValue = line.fields.traceID ?? ""

            case .content:
                textField.font = monoRegularFont
                if line.exceedsMaxLength {
                    textField.textColor = .secondaryLabelColor
                    textField.stringValue = "[oversized line: \(line.byteLength) bytes skipped]"
                } else {
                    textField.textColor = .labelColor
                    textField.stringValue = line.content
                }
            }

            return cellView
        }

        func tableViewSelectionDidChange(_ notification: Notification) {
            guard !isProgrammaticUpdate, let tableView else { return }
            let row = tableView.selectedRow
            if row >= 0, let line = lineCache.line(atRow: row) {
                selectedLineNumberBinding.wrappedValue = line.lineNumber
            } else {
                selectedLineNumberBinding.wrappedValue = nil
            }
        }

        private func reusableCellView(in tableView: NSTableView, columnSpec: ColumnSpec) -> NSTableCellView {
            if let existing = tableView.makeView(withIdentifier: columnSpec.identifier, owner: nil) as? NSTableCellView {
                return existing
            }

            let cell = NSTableCellView()
            cell.identifier = columnSpec.identifier

            let textField = NSTextField(labelWithString: "")
            textField.translatesAutoresizingMaskIntoConstraints = false
            textField.isBezeled = false
            textField.drawsBackground = false
            textField.isEditable = false
            textField.isSelectable = false
            textField.lineBreakMode = .byTruncatingTail
            textField.maximumNumberOfLines = 1
            textField.alignment = columnSpec.alignment
            textField.cell?.truncatesLastVisibleLine = true

            cell.addSubview(textField)
            cell.textField = textField

            NSLayoutConstraint.activate([
                textField.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 2),
                textField.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -2),
                textField.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            ])

            return cell
        }

        private func levelColor(for level: String) -> NSColor {
            switch level.uppercased() {
            case "ERROR", "FATAL", "CRITICAL":
                return .systemRed
            case "WARN", "WARNING":
                return .systemOrange
            case "INFO":
                return .systemBlue
            default:
                return .secondaryLabelColor
            }
        }
    }
}
