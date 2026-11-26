#include "logloom/search_engine.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <regex>
#include <thread>
#include <vector>

namespace logloom {
namespace {

constexpr unsigned char to_lower_ascii(unsigned char ch) noexcept {
  return (ch >= 'A' && ch <= 'Z')
             ? static_cast<unsigned char>(ch + ('a' - 'A'))
             : ch;
}

[[nodiscard]] bool equals_icase(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (to_lower_ascii(static_cast<unsigned char>(a[i])) !=
        to_lower_ascii(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool contains_icase(
    std::string_view haystack,
    std::string_view needle) noexcept {
  if (needle.empty()) {
    return true;
  }
  const auto it = std::search(
      haystack.begin(),
      haystack.end(),
      needle.begin(),
      needle.end(),
      [](char lhs, char rhs) noexcept {
        return to_lower_ascii(static_cast<unsigned char>(lhs)) ==
               to_lower_ascii(static_cast<unsigned char>(rhs));
      });
  return it != haystack.end();
}

[[nodiscard]] bool parse_hex4(std::string_view s, std::size_t pos, std::uint32_t& out) noexcept {
  if (pos + 4 > s.size()) {
    return false;
  }
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    const char ch = s[pos + i];
    value <<= 4U;
    if (ch >= '0' && ch <= '9') {
      value |= static_cast<std::uint32_t>(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      value |= static_cast<std::uint32_t>(10 + (ch - 'a'));
    } else if (ch >= 'A' && ch <= 'F') {
      value |= static_cast<std::uint32_t>(10 + (ch - 'A'));
    } else {
      return false;
    }
  }
  out = value;
  return true;
}

void append_utf8(std::uint32_t cp, std::string& out) {
  if (cp <= 0x7FU) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FFU) {
    out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else if (cp <= 0xFFFFU) {
    out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  }
}

[[nodiscard]] bool unescape_json_string(std::string_view raw, std::string& out) {
  out.clear();
  out.reserve(raw.size());

  for (std::size_t i = 0; i < raw.size(); ++i) {
    const char ch = raw[i];
    if (ch != '\\') {
      out.push_back(ch);
      continue;
    }
    if (i + 1 >= raw.size()) {
      return false;
    }
    const char esc = raw[++i];
    switch (esc) {
      case '"':
      case '\\':
      case '/':
        out.push_back(esc);
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'u': {
        std::uint32_t cp = 0;
        if (!parse_hex4(raw, i + 1, cp)) {
          return false;
        }
        i += 4;
        if (cp >= 0xD800U && cp <= 0xDBFFU && i + 6 < raw.size() &&
            raw[i + 1] == '\\' && raw[i + 2] == 'u') {
          std::uint32_t low = 0;
          if (parse_hex4(raw, i + 3, low) && low >= 0xDC00U && low <= 0xDFFFU) {
            cp = 0x10000U + (((cp - 0xD800U) << 10U) | (low - 0xDC00U));
            i += 6;
          }
        }
        append_utf8(cp, out);
        break;
      }
      default:
        return false;
    }
  }
  return true;
}

[[nodiscard]] bool compile_query_regex(
    const SearchQuery& query,
    std::optional<std::regex>& out_regex,
    std::error_code& ec) {
  out_regex.reset();
  if (!query.is_regex || !query.text_query.has_value()) {
    return true;
  }

  auto flags = std::regex::ECMAScript;
  if (!query.case_sensitive) {
    flags |= std::regex::icase;
  }
  try {
    out_regex.emplace(*query.text_query, flags);
  } catch (const std::regex_error&) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }
  return true;
}

[[nodiscard]] bool evaluate_line_match(
    const LineView& view,
    const ExtractedFields& fields,
    const SearchQuery& query,
    const std::optional<std::regex>& compiled_regex,
    std::string& unescape_buf) {
  if (query.trace_id.has_value()) {
    if (fields.kind != LineKind::JsonObject || !fields.trace_id.has_value()) {
      return false;
    }
    if (!fields.has_escapes_in_trace_id) {
      if (*fields.trace_id != *query.trace_id) {
        return false;
      }
    } else {
      if (!unescape_json_string(*fields.trace_id, unescape_buf) ||
          unescape_buf != *query.trace_id) {
        return false;
      }
    }
  }

  if (query.level.has_value()) {
    if (fields.kind != LineKind::JsonObject || !fields.level.has_value() ||
        !equals_icase(*fields.level, *query.level)) {
      return false;
    }
  }

  if (query.text_query.has_value()) {
    if (compiled_regex.has_value()) {
      return std::regex_search(
          view.content.begin(), view.content.end(), *compiled_regex);
    }
    if (query.case_sensitive) {
      return view.content.find(*query.text_query) != std::string_view::npos;
    }
    return contains_icase(view.content, *query.text_query);
  }

  return true;
}

struct FilePartition {
  std::uint64_t start = 0;
  std::uint64_t end = 0;
};

[[nodiscard]] std::vector<FilePartition> partition_file_on_newlines(
    const PosixFile& file,
    std::size_t partition_bytes,
    bool strip_utf8_bom,
    std::error_code& ec) {
  std::vector<FilePartition> partitions;
  const std::uint64_t file_size = file.size();
  if (file_size == 0) {
    return partitions;
  }

  std::uint64_t effective_start = 0;
  if (strip_utf8_bom) {
    std::array<char, 3> bom{};
    const std::size_t n = file.pread_full(0, bom, ec);
    if (ec) {
      return {};
    }
    if (n == 3 &&
        static_cast<unsigned char>(bom[0]) == 0xEF &&
        static_cast<unsigned char>(bom[1]) == 0xBB &&
        static_cast<unsigned char>(bom[2]) == 0xBF) {
      effective_start = 3;
    }
  }

  if (effective_start >= file_size) {
    return partitions;
  }

  const std::uint64_t part_size = std::max<std::size_t>(partition_bytes, 1);
  constexpr std::size_t kScanBufSize = 64 * 1024;
  std::vector<char> scan_buf(kScanBufSize);

  std::uint64_t cur = effective_start;
  while (cur < file_size) {
    if (file_size - cur <= part_size) {
      partitions.push_back(FilePartition{cur, file_size});
      break;
    }

    std::uint64_t scan_pos = cur + part_size - 1;
    bool found_nl = false;
    while (scan_pos < file_size) {
      const std::size_t n = file.pread_full(scan_pos, scan_buf, ec);
      if (ec) {
        return {};
      }
      if (n == 0) {
        break;
      }
      const void* nl = std::memchr(scan_buf.data(), '\n', n);
      if (nl != nullptr) {
        const auto offset_in_buf =
            static_cast<std::uint64_t>(static_cast<const char*>(nl) - scan_buf.data());
        const std::uint64_t part_end = scan_pos + offset_in_buf + 1ULL;
        partitions.push_back(FilePartition{cur, part_end});
        cur = part_end;
        found_nl = true;
        break;
      }
      scan_pos += n;
    }

    if (!found_nl) {
      partitions.push_back(FilePartition{cur, file_size});
      break;
    }
  }

  return partitions;
}

struct OwnedMatch {
  std::uint64_t local_line_number = 0;
  std::uint64_t byte_offset = 0;
  std::uint64_t byte_length = 0;
  std::string content{};
  bool has_crlf = false;
  bool is_truncated_eof = false;
};

struct PartitionResult {
  std::error_code ec{};
  SearchStats stats{};
  std::vector<OwnedMatch> matches{};
};

void accumulate_line_kind(LineKind kind, SearchStats& stats) noexcept {
  switch (kind) {
    case LineKind::JsonObject:
      ++stats.json_lines;
      break;
    case LineKind::PlainText:
      ++stats.plain_text_lines;
      break;
    case LineKind::Malformed:
      ++stats.malformed_lines;
      break;
  }
}

void merge_partition_stats(const SearchStats& part, SearchStats& total) noexcept {
  total.lines_scanned += part.lines_scanned;
  total.json_lines += part.json_lines;
  total.plain_text_lines += part.plain_text_lines;
  total.malformed_lines += part.malformed_lines;
  total.oversized_lines += part.oversized_lines;
  total.truncated_lines += part.truncated_lines;
  if (part.has_truncated_final_line) {
    total.has_truncated_final_line = true;
  }
}

struct ThreadPoolGuard {
  std::atomic<bool>& internal_cancel;
  std::condition_variable& cv_work;
  std::vector<std::thread>& threads;

  ~ThreadPoolGuard() {
    internal_cancel.store(true, std::memory_order_release);
    cv_work.notify_all();
    for (auto& t : threads) {
      if (t.joinable()) {
        t.join();
      }
    }
  }
};

} // namespace

SearchStats search_file(
    const PosixFile& file,
    const SearchQuery& query,
    const MatchCallback& on_match,
    std::error_code& ec,
    const std::atomic<bool>* cancel_flag) {
  ec.clear();
  SearchStats stats{};

  if (!file.is_open()) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    return stats;
  }

  std::optional<std::regex> compiled_regex;
  if (!compile_query_regex(query, compiled_regex, ec)) {
    return stats;
  }

  const JsonlExtractOptions extract_opts{
      .trace_key = query.trace_key,
      .timestamp_key = query.timestamp_key,
      .level_key = query.level_key,
  };

  ChunkedLineReader reader(file, query.reader_options);
  LineView view{};
  std::string unescape_buf{};

  while (true) {
    if (cancel_flag != nullptr && cancel_flag->load(std::memory_order_relaxed)) {
      stats.cancelled = true;
      break;
    }

    if (!reader.next_line(view, ec)) {
      break;
    }

    ++stats.lines_scanned;
    if (view.is_truncated_eof) {
      ++stats.truncated_lines;
      stats.has_truncated_final_line = true;
    }
    if (view.exceeds_max_length) {
      ++stats.oversized_lines;
      continue;
    }

    const ExtractedFields fields = extract_jsonl_fields(view.content, extract_opts);
    accumulate_line_kind(fields.kind, stats);

    if (!evaluate_line_match(view, fields, query, compiled_regex, unescape_buf)) {
      continue;
    }

    ++stats.matches_found;

    if (on_match) {
      const SearchMatch match{
          .line_number = view.line_number,
          .byte_offset = view.byte_offset,
          .byte_length = view.byte_length,
          .content = view.content,
          .fields = fields,
          .has_crlf = view.has_crlf,
          .is_truncated_eof = view.is_truncated_eof,
      };
      if (!on_match(match)) {
        stats.cancelled = true;
        break;
      }
    }

    if (query.limit > 0 && stats.matches_found >= query.limit) {
      stats.limit_reached = true;
      break;
    }
  }

  return stats;
}

SearchStats search_file_parallel(
    const PosixFile& file,
    const SearchQuery& query,
    const MatchCallback& on_match,
    std::error_code& ec,
    ParallelSearchOptions parallel_options,
    const std::atomic<bool>* cancel_flag) {
  ec.clear();

  if (!file.is_open()) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    return SearchStats{};
  }

  std::optional<std::regex> regex_check;
  if (!compile_query_regex(query, regex_check, ec)) {
    return SearchStats{};
  }
  regex_check.reset();

  if (cancel_flag != nullptr && cancel_flag->load(std::memory_order_relaxed)) {
    return SearchStats{.cancelled = true};
  }

  const auto partitions = partition_file_on_newlines(
      file,
      parallel_options.partition_bytes,
      query.reader_options.strip_utf8_bom,
      ec);
  if (ec) {
    return SearchStats{};
  }
  if (partitions.empty()) {
    return SearchStats{};
  }

  std::size_t worker_count = parallel_options.num_threads;
  if (worker_count == 0) {
    const unsigned int hw = std::thread::hardware_concurrency();
    worker_count = hw > 0 ? static_cast<std::size_t>(hw) : 1U;
  }
  worker_count = std::min(worker_count, partitions.size());

  if (worker_count <= 1 || partitions.size() <= 1) {
    return search_file(file, query, on_match, ec, cancel_flag);
  }

  const JsonlExtractOptions extract_opts{
      .trace_key = query.trace_key,
      .timestamp_key = query.timestamp_key,
      .level_key = query.level_key,
  };

  const std::size_t max_in_flight = std::max<std::size_t>(worker_count * 2, 2);
  std::mutex mu;
  std::condition_variable cv_work;
  std::condition_variable cv_done;
  std::size_t next_to_claim = 0;
  std::size_t next_to_emit = 0;
  std::size_t active_workers = worker_count;
  std::vector<std::optional<PartitionResult>> completed(partitions.size());
  std::atomic<bool> internal_cancel{false};
  std::vector<std::thread> threads;
  threads.reserve(worker_count);

  ThreadPoolGuard pool_guard{internal_cancel, cv_work, threads};

  auto worker_fn = [&]() {
    std::optional<std::regex> worker_regex;
    std::error_code regex_ec;
    if (!compile_query_regex(query, worker_regex, regex_ec)) {
      std::lock_guard<std::mutex> lock(mu);
      --active_workers;
      cv_done.notify_one();
      return;
    }
    std::string unescape_buf;

    while (true) {
      std::size_t part_idx = 0;
      {
        std::unique_lock<std::mutex> lock(mu);
        cv_work.wait(lock, [&] {
          return internal_cancel.load(std::memory_order_acquire) ||
                 (cancel_flag != nullptr && cancel_flag->load(std::memory_order_relaxed)) ||
                 next_to_claim >= partitions.size() ||
                 (next_to_claim - next_to_emit) < max_in_flight;
        });

        if (internal_cancel.load(std::memory_order_acquire) ||
            (cancel_flag != nullptr && cancel_flag->load(std::memory_order_relaxed)) ||
            next_to_claim >= partitions.size()) {
          break;
        }
        part_idx = next_to_claim++;
      }

      const FilePartition part = partitions[part_idx];
      PartitionResult part_res{};

      LineReaderOptions reader_opts = query.reader_options;
      reader_opts.strip_utf8_bom = false;
      reader_opts.start_offset = part.start;
      reader_opts.end_offset = part.end;

      ChunkedLineReader reader(file, reader_opts);
      LineView view{};

      while (true) {
        if (internal_cancel.load(std::memory_order_acquire) ||
            (cancel_flag != nullptr && cancel_flag->load(std::memory_order_relaxed))) {
          part_res.stats.cancelled = true;
          break;
        }

        if (!reader.next_line(view, part_res.ec)) {
          break;
        }

        ++part_res.stats.lines_scanned;
        if (view.is_truncated_eof) {
          ++part_res.stats.truncated_lines;
          part_res.stats.has_truncated_final_line = true;
        }
        if (view.exceeds_max_length) {
          ++part_res.stats.oversized_lines;
          continue;
        }

        const ExtractedFields fields = extract_jsonl_fields(view.content, extract_opts);
        accumulate_line_kind(fields.kind, part_res.stats);

        if (!evaluate_line_match(view, fields, query, worker_regex, unescape_buf)) {
          continue;
        }

        ++part_res.stats.matches_found;
        part_res.matches.push_back(OwnedMatch{
            .local_line_number = view.line_number,
            .byte_offset = view.byte_offset,
            .byte_length = view.byte_length,
            .content = std::string(view.content),
            .has_crlf = view.has_crlf,
            .is_truncated_eof = view.is_truncated_eof,
        });
      }

      if (part_res.ec) {
        internal_cancel.store(true, std::memory_order_release);
      }

      {
        std::lock_guard<std::mutex> lock(mu);
        completed[part_idx] = std::move(part_res);
      }
      cv_done.notify_one();
      if (internal_cancel.load(std::memory_order_acquire)) {
        cv_work.notify_all();
      }
    }

    {
      std::lock_guard<std::mutex> lock(mu);
      --active_workers;
    }
    cv_done.notify_one();
  };

  for (std::size_t i = 0; i < worker_count; ++i) {
    threads.emplace_back(worker_fn);
  }

  SearchStats total_stats{};
  std::uint64_t line_offset = 0;

  for (std::size_t emit_idx = 0; emit_idx < partitions.size(); ++emit_idx) {
    PartitionResult part_res{};
    {
      std::unique_lock<std::mutex> lock(mu);
      cv_done.wait(lock, [&] {
        return completed[emit_idx].has_value() || active_workers == 0;
      });

      if (!completed[emit_idx].has_value()) {
        if (cancel_flag != nullptr && cancel_flag->load(std::memory_order_relaxed)) {
          total_stats.cancelled = true;
        }
        break;
      }

      part_res = std::move(*completed[emit_idx]);
      completed[emit_idx].reset();
      next_to_emit = emit_idx + 1;
    }
    cv_work.notify_all();

    if (part_res.ec) {
      ec = part_res.ec;
      internal_cancel.store(true, std::memory_order_release);
      cv_work.notify_all();
      break;
    }

    merge_partition_stats(part_res.stats, total_stats);

    bool stop_emitting = false;
    for (const auto& owned : part_res.matches) {
      if (cancel_flag != nullptr && cancel_flag->load(std::memory_order_relaxed)) {
        total_stats.cancelled = true;
        stop_emitting = true;
        break;
      }

      ++total_stats.matches_found;

      if (on_match) {
        const ExtractedFields fields = extract_jsonl_fields(owned.content, extract_opts);
        const SearchMatch match{
            .line_number = line_offset + owned.local_line_number,
            .byte_offset = owned.byte_offset,
            .byte_length = owned.byte_length,
            .content = owned.content,
            .fields = fields,
            .has_crlf = owned.has_crlf,
            .is_truncated_eof = owned.is_truncated_eof,
        };
        if (!on_match(match)) {
          total_stats.cancelled = true;
          stop_emitting = true;
          break;
        }
      }

      if (query.limit > 0 && total_stats.matches_found >= query.limit) {
        total_stats.limit_reached = true;
        stop_emitting = true;
        break;
      }
    }

    if (part_res.stats.cancelled && !total_stats.limit_reached) {
      total_stats.cancelled = true;
      stop_emitting = true;
    }

    if (stop_emitting) {
      internal_cancel.store(true, std::memory_order_release);
      cv_work.notify_all();
      break;
    }

    line_offset += part_res.stats.lines_scanned;
  }

  return total_stats;
}

} // namespace logloom
