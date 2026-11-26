#pragma once

#include "logloom/jsonl_extractor.hpp"
#include "logloom/line_reader.hpp"
#include "logloom/posix_file.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace logloom {

struct SearchQuery {
  std::optional<std::string> trace_id{};
  std::string trace_key = "request_id";
  std::optional<std::string> level{};
  std::string level_key = "level";
  std::string timestamp_key = "timestamp";
  std::optional<std::string> text_query{};
  bool case_sensitive = true;
  bool is_regex = false;
  std::uint64_t limit = 0; // 0 = unlimited
  LineReaderOptions reader_options{};
};

struct SearchMatch {
  std::uint64_t line_number = 0;
  std::uint64_t byte_offset = 0;
  std::uint64_t byte_length = 0;
  std::string_view content{};
  ExtractedFields fields{};
  bool has_crlf = false;
  bool is_truncated_eof = false;
};

struct SearchStats {
  std::uint64_t lines_scanned = 0;
  std::uint64_t matches_found = 0;
  std::uint64_t json_lines = 0;
  std::uint64_t plain_text_lines = 0;
  std::uint64_t malformed_lines = 0;
  std::uint64_t oversized_lines = 0;
  std::uint64_t truncated_lines = 0;
  bool has_truncated_final_line = false;
  bool cancelled = false;
  bool limit_reached = false;
};

using MatchCallback = std::function<bool(const SearchMatch&)>;

struct ParallelSearchOptions {
  std::size_t num_threads = 0; // 0 = auto (hardware_concurrency)
  std::size_t partition_bytes = 4 * 1024 * 1024;
};

/// Streams `file` line-by-line, evaluates active query filters (AND semantics),
/// and invokes `on_match` immediately as matches are found.
[[nodiscard]] SearchStats search_file(
    const PosixFile& file,
    const SearchQuery& query,
    const MatchCallback& on_match,
    std::error_code& ec,
    const std::atomic<bool>* cancel_flag = nullptr);

/// Partitions `file` on exact newline boundaries across a bounded worker pool
/// and streams matches in deterministic file order.
[[nodiscard]] SearchStats search_file_parallel(
    const PosixFile& file,
    const SearchQuery& query,
    const MatchCallback& on_match,
    std::error_code& ec,
    ParallelSearchOptions parallel_options = {},
    const std::atomic<bool>* cancel_flag = nullptr);

} // namespace logloom
