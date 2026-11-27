import AppKit
import SwiftUI

/// Restrained dark/light adaptive palette aligned with Logloom's visual identity.
enum LogloomTheme {
    private static func dynamicNSColor(dark: UInt32, light: UInt32, alpha: CGFloat = 1.0) -> NSColor {
        NSColor(name: nil) { appearance in
            let isDark = appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
            return colorFromHex(isDark ? dark : light, alpha: alpha)
        }
    }

    private static func colorFromHex(_ hex: UInt32, alpha: CGFloat = 1.0) -> NSColor {
        let r = CGFloat((hex >> 16) & 0xFF) / 255.0
        let g = CGFloat((hex >> 8) & 0xFF) / 255.0
        let b = CGFloat(hex & 0xFF) / 255.0
        return NSColor(srgbRed: r, green: g, blue: b, alpha: alpha)
    }

    // Surfaces
    static let nsCanvas = dynamicNSColor(dark: 0x222529, light: 0xF7F6F3)
    static let nsPanel = dynamicNSColor(dark: 0x292C30, light: 0xEEEDE9)
    static let nsTitlebar = dynamicNSColor(dark: 0x2D3033, light: 0xEAE9E4)
    static let nsTableHeader = dynamicNSColor(dark: 0x2A2D31, light: 0xEFEEEA)
    static let nsInputBg = dynamicNSColor(dark: 0x232629, light: 0xFFFFFF)

    // Borders
    static let nsBorder = dynamicNSColor(dark: 0x393D41, light: 0xDAD8D2)
    static let nsInputBorder = dynamicNSColor(dark: 0x46494D, light: 0xC9C6BF)
    static let nsInputFocusedBorder = dynamicNSColor(dark: 0xAD806E, light: 0xB86D4B)

    // Text
    static let nsTextPrimary = dynamicNSColor(dark: 0xE2E0DA, light: 0x1F2226)
    static let nsTextSoft = dynamicNSColor(dark: 0xB2B4B3, light: 0x55595E)
    static let nsTextMuted = dynamicNSColor(dark: 0x949A9D, light: 0x6F747A)

    // Brand Copper Accent
    static let nsCopper = dynamicNSColor(dark: 0xE4A183, light: 0xB45C36)
    static let nsCopperUnderline = dynamicNSColor(dark: 0xC38D73, light: 0xB45C36)
    static let nsCopperBorder = dynamicNSColor(dark: 0xB77F65, light: 0xC27351)

    // Selection & State Highlights
    static let nsSelectedRowBg = dynamicNSColor(dark: 0x373533, light: 0xEFE4DC)
    static let nsSelectedRowUnfocusedBg = dynamicNSColor(dark: 0x31302E, light: 0xEAE4E0)
    static let nsSelectedLineNum = dynamicNSColor(dark: 0xEAC2AD, light: 0x8F4322)
    static let nsToggleOnBg = dynamicNSColor(dark: 0x49403C, light: 0xEBD8CE)
    static let nsTraceBoxBg = dynamicNSColor(dark: 0x35312F, light: 0xF3EAE3)
    static let nsTraceStripBg = dynamicNSColor(dark: 0x39332F, light: 0xF1E4DA)
    static let nsTraceStripBorder = dynamicNSColor(dark: 0x53443D, light: 0xDFC4B4)
    static let nsTraceStripText = dynamicNSColor(dark: 0xECC1A8, light: 0x8B4020)

    // Semantic Log Levels
    static let nsLevelInfo = dynamicNSColor(dark: 0x99BAB0, light: 0x2F6E5D)
    static let nsLevelDebug = dynamicNSColor(dark: 0x9CA6B1, light: 0x5E6B78)
    static let nsLevelWarn = dynamicNSColor(dark: 0xDBC089, light: 0x926B1C)
    static let nsLevelError = dynamicNSColor(dark: 0xE5A19B, light: 0xAD3D36)

    // JSON Syntax Highlighting
    static let nsJsonKey = dynamicNSColor(dark: 0xB2BAC9, light: 0x3E5270)
    static let nsJsonString = dynamicNSColor(dark: 0xB3C8B9, light: 0x2E6B47)
    static let nsJsonNumber = dynamicNSColor(dark: 0xD5B69E, light: 0x8E5128)

    // Indicators & Buttons
    static let nsReadOnlyDot = dynamicNSColor(dark: 0x8DAC9C, light: 0x3D7A5E)
    static let nsAccentButtonBg = dynamicNSColor(dark: 0xDFAA8F, light: 0xB85F39)
    static let nsAccentButtonFg = dynamicNSColor(dark: 0x241F1D, light: 0xFFFFFF)

    // SwiftUI Colors
    static let canvas = Color(nsColor: nsCanvas)
    static let panel = Color(nsColor: nsPanel)
    static let titlebar = Color(nsColor: nsTitlebar)
    static let inputBg = Color(nsColor: nsInputBg)
    static let border = Color(nsColor: nsBorder)
    static let inputBorder = Color(nsColor: nsInputBorder)
    static let inputFocusedBorder = Color(nsColor: nsInputFocusedBorder)
    static let textPrimary = Color(nsColor: nsTextPrimary)
    static let textSoft = Color(nsColor: nsTextSoft)
    static let textMuted = Color(nsColor: nsTextMuted)
    static let copper = Color(nsColor: nsCopper)
    static let copperUnderline = Color(nsColor: nsCopperUnderline)
    static let copperBorder = Color(nsColor: nsCopperBorder)
    static let selectedRowBg = Color(nsColor: nsSelectedRowBg)
    static let toggleOnBg = Color(nsColor: nsToggleOnBg)
    static let traceBoxBg = Color(nsColor: nsTraceBoxBg)
    static let traceStripBg = Color(nsColor: nsTraceStripBg)
    static let traceStripBorder = Color(nsColor: nsTraceStripBorder)
    static let traceStripText = Color(nsColor: nsTraceStripText)
    static let jsonKey = Color(nsColor: nsJsonKey)
    static let jsonString = Color(nsColor: nsJsonString)
    static let jsonNumber = Color(nsColor: nsJsonNumber)
    static let readOnlyDot = Color(nsColor: nsReadOnlyDot)
    static let accentButtonBg = Color(nsColor: nsAccentButtonBg)
    static let accentButtonFg = Color(nsColor: nsAccentButtonFg)

    static func nsLevelColor(for level: String) -> NSColor {
        switch level.uppercased() {
        case "ERROR", "FATAL", "CRITICAL":
            return nsLevelError
        case "WARN", "WARNING":
            return nsLevelWarn
        case "INFO":
            return nsLevelInfo
        case "DEBUG", "TRACE":
            return nsLevelDebug
        default:
            return nsTextMuted
        }
    }

    static func levelColor(for level: String) -> Color {
        Color(nsColor: nsLevelColor(for: level))
    }
}

/// Crisp vector rendering of the Logloom emblem matching `assets/Logloom.svg`.
struct LogloomMarkView: View {
    var size: CGFloat = 24
    var cornerRadius: CGFloat = 5

    var body: some View {
        Canvas { context, canvasSize in
            let s = min(canvasSize.width, canvasSize.height) / 1024.0
            context.scaleBy(x: s, y: s)

            let bgRect = CGRect(x: 0, y: 0, width: 1024, height: 1024)
            let gradient = Gradient(colors: [
                Color(red: 0x10 / 255.0, green: 0x13 / 255.0, blue: 0x29 / 255.0),
                Color(red: 0x08 / 255.0, green: 0x0A / 255.0, blue: 0x1C / 255.0),
            ])
            context.fill(
                Path(bgRect),
                with: .linearGradient(
                    gradient,
                    startPoint: .zero,
                    endPoint: CGPoint(x: 0, y: 1024)
                )
            )

            let cream = Color(red: 0xF5 / 255.0, green: 0xE9 / 255.0, blue: 0xD4 / 255.0)
            let flame = Color(red: 0xFF / 255.0, green: 0x50 / 255.0, blue: 0x1F / 255.0)

            var outer = Path()
            outer.move(to: CGPoint(x: 174, y: 170))
            outer.addLine(to: CGPoint(x: 240, y: 170))
            outer.addLine(to: CGPoint(x: 240, y: 652))
            outer.addArc(
                center: CGPoint(x: 366, y: 652),
                radius: 126,
                startAngle: .degrees(180),
                endAngle: .degrees(90),
                clockwise: true
            )
            outer.addLine(to: CGPoint(x: 850, y: 778))
            outer.addLine(to: CGPoint(x: 850, y: 844))
            outer.addLine(to: CGPoint(x: 366, y: 844))
            outer.addArc(
                center: CGPoint(x: 366, y: 652),
                radius: 192,
                startAngle: .degrees(90),
                endAngle: .degrees(180),
                clockwise: false
            )
            outer.closeSubpath()
            context.fill(outer, with: .color(cream))

            var inner = Path()
            inner.move(to: CGPoint(x: 374, y: 170))
            inner.addLine(to: CGPoint(x: 440, y: 170))
            inner.addLine(to: CGPoint(x: 440, y: 552))
            inner.addArc(
                center: CGPoint(x: 474, y: 552),
                radius: 34,
                startAngle: .degrees(180),
                endAngle: .degrees(90),
                clockwise: true
            )
            inner.addLine(to: CGPoint(x: 676, y: 586))
            inner.addLine(to: CGPoint(x: 676, y: 652))
            inner.addLine(to: CGPoint(x: 474, y: 652))
            inner.addArc(
                center: CGPoint(x: 474, y: 552),
                radius: 100,
                startAngle: .degrees(90),
                endAngle: .degrees(180),
                clockwise: false
            )
            inner.closeSubpath()
            context.fill(inner, with: .color(cream))

            var trace = Path()
            trace.move(to: CGPoint(x: 274, y: 170))
            trace.addLine(to: CGPoint(x: 340, y: 170))
            trace.addLine(to: CGPoint(x: 340, y: 598))
            trace.addArc(
                center: CGPoint(x: 426, y: 598),
                radius: 86,
                startAngle: .degrees(180),
                endAngle: .degrees(90),
                clockwise: true
            )
            trace.addLine(to: CGPoint(x: 776, y: 684))
            trace.addLine(to: CGPoint(x: 710, y: 750))
            trace.addLine(to: CGPoint(x: 426, y: 750))
            trace.addArc(
                center: CGPoint(x: 426, y: 598),
                radius: 152,
                startAngle: .degrees(90),
                endAngle: .degrees(180),
                clockwise: false
            )
            trace.closeSubpath()
            context.fill(trace, with: .color(flame))
        }
        .frame(width: size, height: size)
        .clipShape(RoundedRectangle(cornerRadius: cornerRadius, style: .continuous))
    }
}
