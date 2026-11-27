import AppKit
import LogloomCore
import SwiftUI

enum InspectorTab: String, CaseIterable, Identifiable {
  case formatted = "Formatted"
  case raw = "Raw"
  case context = "Context"

  var id: String { rawValue }
}

struct LineInspectorView: View {
  @ObservedObject var model: AppModel
  @State private var activeTab: InspectorTab = .formatted
  @State private var copiedRecently = false

  private static let contextRadiusOptions = [5, 10, 25, 50]

  var body: some View {
    VStack(spacing: 0) {
      if let line = model.selectedLine {
        detailHeader(for: line)
        tabBar
        Divider().overlay(LogloomTheme.border)

        switch activeTab {
        case .formatted:
          formattedTab(for: line)
        case .raw:
          rawTab(for: line)
        case .context:
          contextTab(for: line)
        }

        Divider().overlay(LogloomTheme.border)
        bottomCopyBar(for: line)
      } else {
        VStack(spacing: 8) {
          Text("Select a log line to inspect")
            .font(.callout)
            .foregroundStyle(LogloomTheme.textSoft)
          Text("Use ⌘G / ⇧⌘G to step rows, ⌘L to jump to a line.")
            .font(.caption)
            .foregroundStyle(LogloomTheme.textMuted)
        }
        .padding(24)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
      }
    }
    .background(LogloomTheme.panel)
  }

  private func detailHeader(for line: LogLine) -> some View {
    VStack(alignment: .leading, spacing: 0) {
      HStack(alignment: .firstTextBaseline, spacing: 10) {
        Text("Line \(String(line.lineNumber))")
          .font(.system(size: 17, weight: .semibold).monospacedDigit())
          .foregroundStyle(LogloomTheme.textPrimary)

        Spacer()

        if let level = line.fields.level, !level.isEmpty {
          let upper = level.uppercased()
          Text(upper)
            .font(.system(size: 10.5, weight: .semibold, design: .monospaced))
            .foregroundStyle(LogloomTheme.levelColor(for: upper))
        }
      }

      HStack(spacing: 8) {
        Text("Offset \(String(line.byteOffset)) · \(String(line.byteLength)) B")
          .font(.system(size: 11, design: .monospaced))
          .foregroundStyle(LogloomTheme.textMuted)

        if line.hasCRLF {
          tagLabel("CRLF")
        }
        if line.isTruncatedEOF {
          tagLabel("Truncated EOF")
        }
      }
      .padding(.top, 6)

      if line.fields.kind == .jsonObject, let msg = line.fields.message, !msg.isEmpty {
        Text(msg)
          .font(.system(size: 17, weight: .medium))
          .foregroundStyle(LogloomTheme.textPrimary)
          .lineSpacing(3)
          .textSelection(.enabled)
          .padding(.top, 14)
      }

      if let traceID = line.fields.traceID, !traceID.isEmpty {
        traceIsolationBox(traceID: traceID)
          .padding(.top, 16)
      }
    }
    .padding(.horizontal, 20)
    .padding(.top, 18)
    .padding(.bottom, 14)
  }

  private func traceIsolationBox(traceID: String) -> some View {
    let isActiveTrace = (model.isolatedTraceID == traceID)
    return HStack(spacing: 10) {
      VStack(alignment: .leading, spacing: 4) {
        Text("Trace ID")
          .font(.system(size: 11))
          .foregroundStyle(LogloomTheme.textSoft)
        Text(traceID)
          .font(.system(size: 12, design: .monospaced))
          .foregroundStyle(LogloomTheme.copper)
          .textSelection(.enabled)
          .lineLimit(1)
      }

      Spacer()

      Button(isActiveTrace ? "Exit trace ×" : "Isolate trace →") {
        if isActiveTrace {
          model.clearTraceIsolation()
        } else {
          model.isolateTrace(traceID)
        }
      }
      .font(.system(size: 12, weight: .medium))
      .foregroundStyle(LogloomTheme.copper)
      .buttonStyle(.plain)
    }
    .padding(.leading, 12)
    .padding(.trailing, 10)
    .padding(.vertical, 10)
    .background(LogloomTheme.traceBoxBg)
    .overlay(alignment: .leading) {
      Rectangle()
        .fill(LogloomTheme.copperBorder)
        .frame(width: 2)
    }
  }

  private var tabBar: some View {
    HStack(spacing: 16) {
      ForEach(InspectorTab.allCases) { tab in
        let isSelected = (activeTab == tab)
        Button {
          activeTab = tab
        } label: {
          VStack(spacing: 7) {
            Text(tab.rawValue)
              .font(.system(size: 12.5, weight: isSelected ? .medium : .regular))
              .foregroundStyle(isSelected ? LogloomTheme.textPrimary : LogloomTheme.textMuted)
            Rectangle()
              .fill(isSelected ? LogloomTheme.copperUnderline : Color.clear)
              .frame(height: 2)
          }
          .padding(.top, 6)
        }
        .buttonStyle(.plain)
      }
      Spacer()
    }
    .padding(.horizontal, 20)
  }

  private func formattedTab(for line: LogLine) -> some View {
    Group {
      if line.exceedsMaxLength {
        oversizedNotice(byteLength: line.byteLength)
      } else if let pretty = JSONFormatter.prettyPrint(line.content) {
        ScrollView([.vertical, .horizontal]) {
          Text(highlightPrettyJSON(pretty))
            .font(.system(size: 12, design: .monospaced))
            .lineSpacing(5)
            .textSelection(.enabled)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(20)
        }
      } else {
        VStack(alignment: .leading, spacing: 0) {
          HStack(spacing: 6) {
            Text(fallbackNote(for: line.fields.kind))
              .font(.caption)
              .foregroundStyle(LogloomTheme.textMuted)
            Spacer()
          }
          .padding(.horizontal, 20)
          .padding(.vertical, 8)

          Divider().overlay(LogloomTheme.border)

          ScrollView([.vertical, .horizontal]) {
            Text(line.content)
              .font(.system(size: 12, design: .monospaced))
              .foregroundStyle(LogloomTheme.textSoft)
              .lineSpacing(5)
              .textSelection(.enabled)
              .frame(maxWidth: .infinity, alignment: .leading)
              .padding(20)
          }
        }
      }
    }
    .frame(maxWidth: .infinity, maxHeight: .infinity)
  }

  private func rawTab(for line: LogLine) -> some View {
    Group {
      if line.exceedsMaxLength {
        oversizedNotice(byteLength: line.byteLength)
      } else {
        ScrollView([.vertical, .horizontal]) {
          Text(line.content)
            .font(.system(size: 12, design: .monospaced))
            .foregroundStyle(LogloomTheme.textSoft)
            .lineSpacing(5)
            .textSelection(.enabled)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(20)
        }
      }
    }
    .frame(maxWidth: .infinity, maxHeight: .infinity)
  }

  private func contextTab(for line: LogLine) -> some View {
    VStack(spacing: 0) {
      HStack(spacing: 8) {
        Text("Radius")
          .font(.caption)
          .foregroundStyle(LogloomTheme.textMuted)

        Picker("Radius", selection: $model.contextRadius) {
          ForEach(Self.contextRadiusOptions, id: \.self) { radius in
            Text("±\(radius)").tag(radius)
          }
        }
        .labelsHidden()
        .pickerStyle(.menu)
        .controlSize(.small)
        .frame(width: 72)

        Spacer()

        if model.isFilterActive {
          Button("Show in full log") {
            model.jumpToLine(line.lineNumber, clearFiltersIfNeeded: true)
          }
          .font(.caption)
          .foregroundStyle(LogloomTheme.copper)
          .buttonStyle(.borderless)
        }
      }
      .padding(.horizontal, 16)
      .padding(.vertical, 7)

      Divider().overlay(LogloomTheme.border)

      if !model.isIndexed {
        VStack(spacing: 6) {
          Text("Indexing in progress")
            .font(.callout)
            .foregroundStyle(LogloomTheme.textSoft)
          Text("Context window becomes available as soon as indexing finishes.")
            .font(.caption)
            .foregroundStyle(LogloomTheme.textMuted)
        }
        .padding(20)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
      } else {
        ScrollView {
          LazyVStack(alignment: .leading, spacing: 0) {
            ForEach(model.contextLines) { ctxLine in
              let isCenter = ctxLine.lineNumber == line.lineNumber
              HStack(alignment: .firstTextBaseline, spacing: 8) {
                Text("\(ctxLine.lineNumber)")
                  .font(.system(size: 11.5, design: .monospaced).monospacedDigit())
                  .foregroundStyle(isCenter ? LogloomTheme.copper : LogloomTheme.textMuted)
                  .frame(minWidth: 44, alignment: .trailing)

                if let lvl = ctxLine.fields.level, !lvl.isEmpty {
                  Text(lvl.uppercased())
                    .font(.system(size: 10.5, weight: .semibold, design: .monospaced))
                    .foregroundStyle(LogloomTheme.levelColor(for: lvl))
                    .frame(minWidth: 44, alignment: .leading)
                }

                Text(
                  ctxLine.exceedsMaxLength
                    ? "[oversized line: \(ctxLine.byteLength) bytes skipped]"
                    : ctxLine.displayMessage
                )
                .font(.system(size: 12, design: .monospaced))
                .foregroundStyle(isCenter ? LogloomTheme.textPrimary : LogloomTheme.textSoft)
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(maxWidth: .infinity, alignment: .leading)
              }
              .padding(.horizontal, 14)
              .padding(.vertical, 5)
              .background(isCenter ? LogloomTheme.selectedRowBg : Color.clear)
              .overlay(alignment: .leading) {
                if isCenter {
                  Rectangle()
                    .fill(LogloomTheme.copper)
                    .frame(width: 2)
                }
              }
              .contentShape(Rectangle())
              .onTapGesture {
                model.jumpToLine(ctxLine.lineNumber, clearFiltersIfNeeded: false)
              }
            }
          }
          .padding(.vertical, 6)
        }
      }
    }
    .frame(maxWidth: .infinity, maxHeight: .infinity)
  }

  private func bottomCopyBar(for line: LogLine) -> some View {
    HStack {
      Button(copiedRecently ? "Copied" : "Copy raw") {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(line.content, forType: .string)
        copiedRecently = true
        Task { @MainActor in
          try? await Task.sleep(nanoseconds: 1_500_000_000)
          copiedRecently = false
        }
      }
      .font(.system(size: 12.5))
      .foregroundStyle(copiedRecently ? LogloomTheme.copper : LogloomTheme.textSoft)
      .buttonStyle(.plain)

      Spacer()
    }
    .padding(.horizontal, 20)
    .padding(.vertical, 10)
  }

  private func oversizedNotice(byteLength: UInt64) -> some View {
    VStack(spacing: 6) {
      Text("Oversized line (\(byteLength) bytes)")
        .font(.callout.monospacedDigit())
        .foregroundStyle(LogloomTheme.textSoft)
      Text("Content exceeds max line cap and was skipped by the reader.")
        .font(.caption)
        .foregroundStyle(LogloomTheme.textMuted)
    }
    .padding(20)
    .frame(maxWidth: .infinity, maxHeight: .infinity)
  }

  private func fallbackNote(for kind: LogLineKind) -> String {
    switch kind {
    case .malformed:
      return "Malformed JSON — showing raw content"
    case .plainText:
      return "Plain text"
    case .jsonObject:
      return "Raw content"
    }
  }

  private func tagLabel(_ text: String) -> some View {
    Text(text)
      .font(.caption2)
      .foregroundStyle(LogloomTheme.textMuted)
      .padding(.horizontal, 5)
      .padding(.vertical, 1)
      .background(LogloomTheme.border.opacity(0.45))
      .clipShape(RoundedRectangle(cornerRadius: 3))
  }

  private func highlightPrettyJSON(_ json: String) -> AttributedString {
    var result = AttributedString(json)
    result.foregroundColor = LogloomTheme.textSoft

    let fullRange = NSRange(json.startIndex..<json.endIndex, in: json)
    let pattern =
      #""(?:\\.|[^"\\])*"\s*:|"(?:\\.|[^"\\])*"|\b(?:true|false|null)\b|-?\d+(?:\.\d+)?"#
    guard let regex = try? NSRegularExpression(pattern: pattern) else {
      return result
    }

    for match in regex.matches(in: json, range: fullRange) {
      guard let swiftRange = Range(match.range, in: json),
        let attrStart = AttributedString.Index(swiftRange.lowerBound, within: result),
        let attrEnd = AttributedString.Index(swiftRange.upperBound, within: result)
      else {
        continue
      }
      let token = json[swiftRange]
      if token.hasSuffix(":") {
        result[attrStart..<attrEnd].foregroundColor = LogloomTheme.jsonKey
      } else if token.hasPrefix("\"") {
        result[attrStart..<attrEnd].foregroundColor = LogloomTheme.jsonString
      } else {
        result[attrStart..<attrEnd].foregroundColor = LogloomTheme.jsonNumber
      }
    }
    return result
  }
}
