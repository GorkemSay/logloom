#include "logloom/search_engine.hpp"
#include "parallel_search_pipeline.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace logloom {
namespace {

[[nodiscard]] std::uint64_t detect_bom_offset(
    const PosixFile& file,
    bool strip_utf8_bom,
    std::error_code& ec) {
  if (!strip_utf8_bom) {
    return 0;
  }
  std::array<char, 3> bom{};
  const std::size_t n = file.pread_full(0, bom, ec);
  if (ec || n < 3) {
    return 0;
  }
  const bool has_bom =
      static_cast<unsigned char>(bom[0]) == 0xEF &&
      static_cast<unsigned char>(bom[1]) == 0xBB &&
      static_cast<unsigned char>(bom[2]) == 0xBF;
  return has_bom ? 3ULL : 0ULL;
}

[[nodiscard]] std::optional<std::uint64_t> find_next_newline(
    const PosixFile& file,
    std::uint64_t scan_start,
    std::uint64_t file_size,
    std::vector<char>& scan_buf,
    std::error_code& ec) {
  std::uint64_t scan_pos = scan_start;
  while (scan_pos < file_size) {
    const std::size_t n = file.pread_full(scan_pos, scan_buf, ec);
    if (ec || n == 0) {
      return std::nullopt;
    }
    const void* nl = std::memchr(scan_buf.data(), '\n', n);
    if (nl != nullptr) {
      const auto offset =
          static_cast<std::uint64_t>(static_cast<const char*>(nl) - scan_buf.data());
      return scan_pos + offset + 1ULL;
    }
    scan_pos += n;
  }
  return std::nullopt;
}

[[nodiscard]] std::vector<detail::ByteRange> partition_file_on_newlines(
    const PosixFile& file,
    std::size_t partition_bytes,
    bool strip_utf8_bom,
    std::error_code& ec) {
  std::vector<detail::ByteRange> partitions;
  const std::uint64_t file_size = file.size();
  if (file_size == 0) {
    return partitions;
  }

  const std::uint64_t effective_start = detect_bom_offset(file, strip_utf8_bom, ec);
  if (ec || effective_start >= file_size) {
    return partitions;
  }

  const std::uint64_t part_size = std::max<std::size_t>(partition_bytes, 1);
  constexpr std::size_t kScanBufSize = 64 * 1024;
  std::vector<char> scan_buf(kScanBufSize);

  std::uint64_t cur = effective_start;
  while (cur < file_size) {
    if (file_size - cur <= part_size) {
      partitions.push_back(detail::ByteRange{cur, file_size});
      break;
    }

    const auto next_nl = find_next_newline(file, cur + part_size - 1, file_size, scan_buf, ec);
    if (ec) {
      return {};
    }
    if (!next_nl.has_value()) {
      partitions.push_back(detail::ByteRange{cur, file_size});
      break;
    }
    partitions.push_back(detail::ByteRange{cur, *next_nl});
    cur = *next_nl;
  }

  return partitions;
}

void scan_pread_partition(
    const PosixFile& file,
    const detail::ByteRange& part,
    const SearchQuery& query,
    const JsonlExtractOptions& extract_opts,
    const std::optional<std::regex>& worker_regex,
    std::string& unescape_buf,
    const std::atomic<bool>& internal_cancel,
    const std::atomic<bool>* cancel_flag,
    bool collect_matches,
    detail::PartitionResult& out_res) {
  LineReaderOptions reader_opts = query.reader_options;
  reader_opts.strip_utf8_bom = false;
  reader_opts.start_offset = part.start;
  reader_opts.end_offset = part.end;

  ChunkedLineReader reader(file, reader_opts);
  LineView view{};

  while (true) {
    if (detail::is_cancelled(internal_cancel, cancel_flag)) {
      out_res.stats.cancelled = true;
      break;
    }
    if (!reader.next_line(view, out_res.ec)) {
      break;
    }

    ++out_res.stats.lines_scanned;
    if (view.is_truncated_eof) {
      ++out_res.stats.truncated_lines;
      out_res.stats.has_truncated_final_line = true;
    }
    if (view.exceeds_max_length) {
      ++out_res.stats.oversized_lines;
      continue;
    }

    const ExtractedFields fields = extract_jsonl_fields(view.content, extract_opts);
    detail::accumulate_line_kind(fields.kind, out_res.stats);

    if (!detail::evaluate_line_match(
            view.content, fields, query, worker_regex, unescape_buf)) {
      continue;
    }

    ++out_res.stats.matches_found;
    if (collect_matches) {
      out_res.matches.push_back(detail::OwnedMatch{
          .local_line_number = view.line_number,
          .byte_offset = view.byte_offset,
          .byte_length = view.byte_length,
          .content = std::string(view.content),
          .has_crlf = view.has_crlf,
          .is_truncated_eof = view.is_truncated_eof,
      });
    }
  }
}

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
  if (!detail::compile_query_regex(query, compiled_regex, ec)) {
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
    detail::accumulate_line_kind(fields.kind, stats);

    if (!detail::evaluate_line_match(
            view.content, fields, query, compiled_regex, unescape_buf)) {
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
  if (!detail::compile_query_regex(query, regex_check, ec)) {
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
  if (ec || partitions.empty()) {
    return SearchStats{};
  }

  const std::size_t worker_count =
      detail::resolve_worker_count(parallel_options.num_threads, partitions.size());
  if (worker_count <= 1 || partitions.size() <= 1) {
    return search_file(file, query, on_match, ec, cancel_flag);
  }

  const bool collect_matches = static_cast<bool>(on_match);
  return detail::run_parallel_partition_search(
      partitions,
      query,
      on_match,
      worker_count,
      ec,
      cancel_flag,
      [&](const detail::ByteRange& part,
          const JsonlExtractOptions& extract_opts,
          const std::optional<std::regex>& worker_regex,
          std::string& unescape_buf,
          const std::atomic<bool>& internal_cancel,
          detail::PartitionResult& out_res) {
        scan_pread_partition(
            file,
            part,
            query,
            extract_opts,
            worker_regex,
            unescape_buf,
            internal_cancel,
            cancel_flag,
            collect_matches,
            out_res);
      });
}

} // namespace logloom
