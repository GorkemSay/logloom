#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace logloom {

enum class LineKind : std::uint8_t {
  JsonObject,
  PlainText,
  Malformed,
};

struct JsonlExtractOptions {
  std::string_view trace_key = "request_id";
  std::string_view timestamp_key = "timestamp";
  std::string_view level_key = "level";
};

struct ExtractedFields {
  LineKind kind = LineKind::PlainText;
  std::optional<std::string_view> timestamp{};
  std::optional<std::string_view> level{};
  std::optional<std::string_view> trace_id{};
  bool has_escapes_in_trace_id = false;
};

/// Scans a single log line without heap allocations or DOM construction,
/// extracting configured top-level JSON object fields as zero-copy slices.
[[nodiscard]] ExtractedFields extract_jsonl_fields(
    std::string_view line,
    JsonlExtractOptions options = {}) noexcept;

} // namespace logloom
