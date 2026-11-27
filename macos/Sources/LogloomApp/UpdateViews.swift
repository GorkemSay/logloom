import LogloomCore
import SwiftUI

struct UpdateBannerView: View {
  @ObservedObject var updateManager: UpdateManager

  var body: some View {
    if let release = updateManager.availableReleaseForBanner {
      HStack(spacing: 10) {
        Image(systemName: "arrow.down.circle.fill")
          .font(.system(size: 13))
          .foregroundStyle(LogloomTheme.copper)

        Text(bannerText(for: release))
          .font(.system(size: 12, weight: .medium))
          .foregroundStyle(LogloomTheme.traceStripText)
          .lineLimit(1)

        Spacer()

        Button {
          updateManager.isSheetPresented = true
        } label: {
          Text(actionTitle)
            .font(.system(size: 11.5, weight: .semibold))
            .foregroundStyle(LogloomTheme.accentButtonFg)
            .padding(.horizontal, 10)
            .padding(.vertical, 4)
            .background(
              RoundedRectangle(cornerRadius: 5, style: .continuous)
                .fill(LogloomTheme.accentButtonBg)
            )
        }
        .buttonStyle(.plain)

        Button {
          updateManager.isBannerDismissed = true
        } label: {
          Image(systemName: "xmark")
            .font(.system(size: 11, weight: .medium))
            .foregroundStyle(LogloomTheme.textSoft)
            .padding(4)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .help("Dismiss update notice")
      }
      .padding(.horizontal, 16)
      .padding(.vertical, 7)
      .background(LogloomTheme.traceStripBg)
      .overlay(alignment: .bottom) {
        Rectangle().fill(LogloomTheme.traceStripBorder).frame(height: 1)
      }
    }
  }

  private var actionTitle: String {
    switch updateManager.state {
    case .downloading:
      return "Downloading…"
    case .readyToInstall:
      return "Install…"
    default:
      return "Update…"
    }
  }

  private func bannerText(for release: ReleaseInfo) -> String {
    switch updateManager.state {
    case .downloading(_, let progress, _, _):
      let pct = Int((progress * 100).rounded())
      return "Downloading Logloom v\(release.version.description) (\(pct)%)…"
    case .readyToInstall:
      return "Logloom v\(release.version.description) is ready to install."
    default:
      return "Logloom v\(release.version.description) is available."
    }
  }
}

struct UpdateSheetView: View {
  @ObservedObject var updateManager: UpdateManager
  @Environment(\.dismiss) private var dismiss

  var body: some View {
    VStack(alignment: .leading, spacing: 18) {
      header

      Divider().overlay(LogloomTheme.border)

      content

      Divider().overlay(LogloomTheme.border)

      footer
    }
    .padding(22)
    .frame(width: 440)
    .background(LogloomTheme.panel)
  }

  private var header: some View {
    HStack(spacing: 12) {
      LogloomMarkView(size: 40, cornerRadius: 9)

      VStack(alignment: .leading, spacing: 2) {
        Text(headerTitle)
          .font(.system(size: 16, weight: .semibold))
          .foregroundStyle(LogloomTheme.textPrimary)
        Text(headerSubtitle)
          .font(.system(size: 12))
          .foregroundStyle(LogloomTheme.textMuted)
      }

      Spacer()
    }
  }

  private var headerTitle: String {
    switch updateManager.state {
    case .checking:
      return "Checking for Updates"
    case .upToDate:
      return "You’re Up to Date"
    case .updateAvailable:
      return "Update Available"
    case .downloading:
      return "Downloading Update"
    case .readyToInstall:
      return "Ready to Install"
    case .noReleasesPublished:
      return "No Releases Published"
    case .failed:
      return "Update Check Failed"
    case .idle:
      return "Software Update"
    }
  }

  private var headerSubtitle: String {
    switch updateManager.state {
    case .updateAvailable(let current, let release):
      return "v\(current) → v\(release.version.description)"
    case .downloading(let release, _, _, _):
      return "Logloom v\(release.version.description)"
    case .readyToInstall(let release, _):
      return "Logloom v\(release.version.description)"
    case .upToDate(let current), .noReleasesPublished(let current, _):
      return "Current version: v\(current)"
    default:
      return "Logloom Desktop"
    }
  }

  @ViewBuilder
  private var content: some View {
    switch updateManager.state {
    case .idle, .checking:
      HStack(spacing: 12) {
        ProgressView()
          .controlSize(.small)
        Text("Checking GitHub Releases for a newer version…")
          .font(.callout)
          .foregroundStyle(LogloomTheme.textSoft)
      }
      .padding(.vertical, 8)

    case .upToDate(let currentVersion):
      HStack(spacing: 10) {
        Image(systemName: "checkmark.circle.fill")
          .font(.system(size: 18))
          .foregroundStyle(LogloomTheme.readOnlyDot)
        Text("Logloom v\(currentVersion) is the latest version available.")
          .font(.callout)
          .foregroundStyle(LogloomTheme.textPrimary)
      }
      .padding(.vertical, 6)

    case .noReleasesPublished(let currentVersion, _):
      HStack(alignment: .top, spacing: 10) {
        Image(systemName: "info.circle")
          .font(.system(size: 17))
          .foregroundStyle(LogloomTheme.copper)
        Text("You are running Logloom v\(currentVersion). No published releases are available on GitHub yet.")
          .font(.callout)
          .foregroundStyle(LogloomTheme.textSoft)
      }
      .padding(.vertical, 6)

    case .updateAvailable(_, let release):
      VStack(alignment: .leading, spacing: 10) {
        Text(release.title)
          .font(.system(size: 13.5, weight: .semibold))
          .foregroundStyle(LogloomTheme.textPrimary)

        ScrollView {
          Text(release.releaseNotes.isEmpty ? "No release notes provided." : release.releaseNotes)
            .font(.system(size: 12))
            .foregroundStyle(LogloomTheme.textSoft)
            .frame(maxWidth: .infinity, alignment: .leading)
            .textSelection(.enabled)
        }
        .padding(10)
        .frame(height: 150)
        .background(
          RoundedRectangle(cornerRadius: 6, style: .continuous)
            .fill(LogloomTheme.canvas)
            .overlay(
              RoundedRectangle(cornerRadius: 6, style: .continuous)
                .stroke(LogloomTheme.border, lineWidth: 1)
            )
        )

        if let asset = release.preferredAsset {
          Text("Package: \(asset.name) (\(formatByteCount(asset.sizeBytes)))")
            .font(.caption.monospaced())
            .foregroundStyle(LogloomTheme.textMuted)
        }
      }

    case .downloading(let release, let progress, let written, let total):
      VStack(alignment: .leading, spacing: 10) {
        ProgressView(value: total > 0 ? progress : nil)
          .progressViewStyle(.linear)
          .tint(LogloomTheme.copper)

        HStack {
          Text(release.preferredAsset?.name ?? release.tagName)
            .font(.caption.monospaced())
            .foregroundStyle(LogloomTheme.textSoft)
            .lineLimit(1)
          Spacer()
          if total > 0 {
            Text("\(formatByteCount(written)) / \(formatByteCount(total))")
              .font(.caption.monospacedDigit())
              .foregroundStyle(LogloomTheme.textMuted)
          } else {
            Text(formatByteCount(written))
              .font(.caption.monospacedDigit())
              .foregroundStyle(LogloomTheme.textMuted)
          }
        }
      }
      .padding(.vertical, 6)

    case .readyToInstall(_, let localURL):
      HStack(alignment: .top, spacing: 10) {
        Image(systemName: "checkmark.circle.fill")
          .font(.system(size: 18))
          .foregroundStyle(LogloomTheme.readOnlyDot)
        VStack(alignment: .leading, spacing: 4) {
          Text("Downloaded \(localURL.lastPathComponent)")
            .font(.system(size: 13, weight: .semibold))
            .foregroundStyle(LogloomTheme.textPrimary)
          Text("The update package has been opened. Replace Logloom in Applications to finish updating.")
            .font(.callout)
            .foregroundStyle(LogloomTheme.textSoft)
        }
      }
      .padding(.vertical, 6)

    case .failed(let message, _):
      HStack(alignment: .top, spacing: 10) {
        Image(systemName: "exclamationmark.triangle.fill")
          .font(.system(size: 17))
          .foregroundStyle(LogloomTheme.levelColor(for: "ERROR"))
        Text(message)
          .font(.callout)
          .foregroundStyle(LogloomTheme.textSoft)
      }
      .padding(.vertical, 6)
    }
  }

  @ViewBuilder
  private var footer: some View {
    switch updateManager.state {
    case .idle, .checking:
      HStack {
        Spacer()
        Button("Close") { dismiss() }
      }

    case .upToDate:
      HStack {
        Spacer()
        Button("Done") { dismiss() }
          .keyboardShortcut(.defaultAction)
      }

    case .noReleasesPublished(_, let releasesURL):
      HStack {
        Button("View Releases") {
          updateManager.openURL(releasesURL)
        }
        Spacer()
        Button("Done") { dismiss() }
          .keyboardShortcut(.defaultAction)
      }

    case .updateAvailable(_, let release):
      HStack(spacing: 10) {
        Button("Skip This Version") {
          updateManager.skipVersion(release.version.description)
        }
        .buttonStyle(.borderless)
        .foregroundStyle(LogloomTheme.textMuted)

        Spacer()

        Button("View on GitHub") {
          updateManager.openURL(release.htmlURL)
        }

        Button("Later") {
          dismiss()
        }

        Button {
          updateManager.startUpdate(for: release)
        } label: {
          Text(release.preferredAsset == nil ? "Download from GitHub" : "Download & Update")
            .font(.system(size: 12.5, weight: .semibold))
            .foregroundStyle(LogloomTheme.accentButtonFg)
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(
              RoundedRectangle(cornerRadius: 6, style: .continuous)
                .fill(LogloomTheme.accentButtonBg)
            )
        }
        .buttonStyle(.plain)
      }

    case .downloading:
      HStack {
        Spacer()
        Button("Cancel") {
          updateManager.cancelDownload()
        }
      }

    case .readyToInstall(_, let localURL):
      HStack(spacing: 10) {
        Button("Reveal in Finder") {
          updateManager.revealDownloadedInstaller(localURL)
        }
        Spacer()
        Button("Open Installer") {
          updateManager.openDownloadedInstaller(localURL)
        }
        Button("Done") {
          dismiss()
        }
        .keyboardShortcut(.defaultAction)
      }

    case .failed(_, let releasesURL):
      HStack(spacing: 10) {
        Button("Open Releases") {
          updateManager.openURL(releasesURL)
        }
        Spacer()
        Button("Close") {
          dismiss()
        }
        Button("Retry") {
          updateManager.checkForUpdates(userInitiated: true)
        }
        .keyboardShortcut(.defaultAction)
      }
    }
  }

  private func formatByteCount(_ bytes: Int64) -> String {
    ByteCountFormatter.string(fromByteCount: max(bytes, 0), countStyle: .file)
  }
}
