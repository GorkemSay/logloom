#include "logloom/mmap_reader.hpp"
#include "parallel_search_pipeline.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <sys/mman.h>
#include <utility>
#include <vector>

namespace logloom {
namespace {

[[nodiscard]] std::size_t detect_mapped_bom_offset(
    std::string_view view,
    bool strip_utf8_bom) noexcept {
  if (!strip_utf8_bom || view.size() < 3) {
    return 0;
  }
  const bool has_bom =
      static_cast<unsigned char>(view[0]) == 0xEF &&
      static_cast<unsigned char>(view[1]) == 0xBB &&
      static_cast<unsigned char>(view[2]) == 0xBF;
  return has_bom ? 3U : 0U;
}

[[nodiscard]] std::vector<detail::ByteRange> partition_mapped_on_newlines(
    std::string_view view,
    std::size_t effective_start,
    std::size_t partition_bytes) {
  std::vector<detail::ByteRange> partitions;
  const std::size_t total = view.size();
  if (effective_start >= total) {
    return partitions;
  }

  const std::size_t part_size = std::max<std::size_t>(partition_bytes, 1);
  std::size_t cur = effective_start;

  while (cur < total) {
    if (total - cur <= part_size) {
      partitions.push_back(detail::ByteRange{cur, total});
      break;
    }

    const std::size_t scan_pos = cur + part_size - 1;
    const void* nl = std::memchr(view.data() + scan_pos, '\n', total - scan_pos);
    if (nl == nullptr) {
      partitions.push_back(detail::ByteRange{cur, total});
      break;
    }

    const auto nl_idx = static_cast<std::size_t>(static_cast<const char*>(nl) - view.data());
    const std::size_t part_end = nl_idx + 1;
    partitions.push_back(detail::ByteRange{cur, part_end});
    cur = part_end;
  }

  return partitions;
}

template <typename EmitFn>
void scan_mapped_range(
    std::string_view full_view,
    std::size_t range_start,
    std::size_t range_end,
    const SearchQuery& query,
    const JsonlExtractOptions& extract_opts,
    const std::optional<std::regex>& compiled_regex,
    std::string& unescape_buf,
    SearchStats& stats,
    const std::atomic<bool>* cancel_flag,
    const std::atomic<bool>* internal_cancel,
    EmitFn&& emit_match) {
  const char* base = full_view.data();
  std::size_t pos = range_start;
  std::uint64_t line_no = 0;
  const std::size_t max_line_bytes = query.reader_options.max_line_bytes;

  while (pos < range_end) {
    if ((internal_cancel != nullptr && internal_cancel->load(std::memory_order_acquire)) ||
        (cancel_flag != nullptr && cancel_flag->load(std::memory_order_relaxed))) {
      stats.cancelled = true;
      break;
    }

    const std::size_t avail = range_end - pos;
    const void* nl = std::memchr(base + pos, '\n', avail);
    const bool is_truncated_eof = (nl == nullptr);
    const std::size_t raw_len = is_truncated_eof
        ? avail
        : static_cast<std::size_t>(static_cast<const char*>(nl) - (base + pos));
    const std::size_t next_pos = is_truncated_eof ? range_end : (pos + raw_len + 1);

    const bool has_crlf = (!is_truncated_eof && raw_len > 0 && base[pos + raw_len - 1] == '\r');
    const std::size_t payload_len = has_crlf ? (raw_len - 1) : raw_len;
    const std::uint64_t line_offset = static_cast<std::uint64_t>(pos);

    ++line_no;
    ++stats.lines_scanned;
    if (is_truncated_eof) {
      ++stats.truncated_lines;
      stats.has_truncated_final_line = true;
    }

    pos = next_pos;

    if (payload_len > max_line_bytes) {
      ++stats.oversized_lines;
      continue;
    }

    const std::string_view line_content(base + line_offset, payload_len);
    const ExtractedFields fields = extract_jsonl_fields(line_content, extract_opts);
    detail::accumulate_line_kind(fields.kind, stats);

    if (!detail::evaluate_line_match(
            line_content, fields, query, compiled_regex, unescape_buf)) {
      continue;
    }

    ++stats.matches_found;
    if (!emit_match(line_no, line_offset, payload_len, line_content, fields, has_crlf,
                    is_truncated_eof)) {
      break;
    }
  }
}

[[nodiscard]] SearchStats search_mapped_single(
    std::string_view full_view,
    std::size_t effective_start,
    const SearchQuery& query,
    const JsonlExtractOptions& extract_opts,
    const std::optional<std::regex>& compiled_regex,
    const MatchCallback& on_match,
    const std::atomic<bool>* cancel_flag) {
  SearchStats stats{};
  std::string unescape_buf;
  scan_mapped_range(
      full_view,
      effective_start,
      full_view.size(),
      query,
      extract_opts,
      compiled_regex,
      unescape_buf,
      stats,
      cancel_flag,
      nullptr,
      [&](std::uint64_t line_no,
          std::uint64_t byte_offset,
          std::uint64_t byte_length,
          std::string_view content,
          const ExtractedFields& fields,
          bool has_crlf,
          bool is_truncated_eof) {
        if (on_match) {
          const SearchMatch match{
              .line_number = line_no,
              .byte_offset = byte_offset,
              .byte_length = byte_length,
              .content = content,
              .fields = fields,
              .has_crlf = has_crlf,
              .is_truncated_eof = is_truncated_eof,
          };
          if (!on_match(match)) {
            stats.cancelled = true;
            return false;
          }
        }
        if (query.limit > 0 && stats.matches_found >= query.limit) {
          stats.limit_reached = true;
          return false;
        }
        return true;
      });
  return stats;
}

} // namespace

MmapFile::MmapFile(const char* data, std::uint64_t size, bool mapped) noexcept
    : data_(data), size_(size), mapped_(mapped) {}

MmapFile::~MmapFile() {
  unmap();
}

MmapFile::MmapFile(MmapFile&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      mapped_(std::exchange(other.mapped_, false)) {}

MmapFile& MmapFile::operator=(MmapFile&& other) noexcept {
  if (this != &other) {
    unmap();
    data_ = std::exchange(other.data_, nullptr);
    size_ = std::exchange(other.size_, 0);
    mapped_ = std::exchange(other.mapped_, false);
  }
  return *this;
}

void MmapFile::unmap() noexcept {
  if (data_ != nullptr && size_ > 0) {
    ::munmap(const_cast<char*>(data_), static_cast<std::size_t>(size_));
  }
  data_ = nullptr;
  size_ = 0;
  mapped_ = false;
}

std::optional<MmapFile> MmapFile::map_read_only(
    const PosixFile& file,
    std::error_code& ec) noexcept {
  ec.clear();

  if (!file.is_open()) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    return std::nullopt;
  }

  const std::uint64_t len = file.size();
  if (len == 0) {
    return MmapFile(nullptr, 0, true);
  }

  if (len > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    ec = std::make_error_code(std::errc::value_too_large);
    return std::nullopt;
  }

  const auto map_len = static_cast<std::size_t>(len);
  void* ptr = ::mmap(nullptr, map_len, PROT_READ, MAP_PRIVATE, file.fd(), 0);
  if (ptr == MAP_FAILED) {
    ec = std::error_code(errno, std::generic_category());
    return std::nullopt;
  }

  (void)::madvise(ptr, map_len, MADV_SEQUENTIAL);
  return MmapFile(static_cast<const char*>(ptr), len, true);
}

bool MmapFile::is_mapped() const noexcept {
  return mapped_;
}

const char* MmapFile::data() const noexcept {
  return data_;
}

std::uint64_t MmapFile::size() const noexcept {
  return size_;
}

std::string_view MmapFile::view() const noexcept {
  if (data_ == nullptr || size_ == 0) {
    return {};
  }
  return std::string_view(data_, static_cast<std::size_t>(size_));
}

SearchStats search_file_mmap(
    const MmapFile& mapped,
    const SearchQuery& query,
    const MatchCallback& on_match,
    std::error_code& ec,
    ParallelSearchOptions parallel_options,
    const std::atomic<bool>* cancel_flag) {
  ec.clear();

  if (!mapped.is_mapped()) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    return SearchStats{};
  }

  std::optional<std::regex> compiled_regex;
  if (!detail::compile_query_regex(query, compiled_regex, ec)) {
    return SearchStats{};
  }

  if (cancel_flag != nullptr && cancel_flag->load(std::memory_order_relaxed)) {
    return SearchStats{.cancelled = true};
  }

  const std::string_view full_view = mapped.view();
  const std::size_t effective_start =
      detect_mapped_bom_offset(full_view, query.reader_options.strip_utf8_bom);
  if (effective_start >= full_view.size()) {
    return SearchStats{};
  }

  const auto partitions = partition_mapped_on_newlines(
      full_view, effective_start, parallel_options.partition_bytes);
  if (partitions.empty()) {
    return SearchStats{};
  }

  const std::size_t worker_count =
      detail::resolve_worker_count(parallel_options.num_threads, partitions.size());

  if (worker_count <= 1 || partitions.size() <= 1) {
    const JsonlExtractOptions extract_opts{
        .trace_key = query.trace_key,
        .timestamp_key = query.timestamp_key,
        .level_key = query.level_key,
    };
    return search_mapped_single(
        full_view, effective_start, query, extract_opts, compiled_regex, on_match, cancel_flag);
  }

  compiled_regex.reset();
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
        scan_mapped_range(
            full_view,
            static_cast<std::size_t>(part.start),
            static_cast<std::size_t>(part.end),
            query,
            extract_opts,
            worker_regex,
            unescape_buf,
            out_res.stats,
            cancel_flag,
            &internal_cancel,
            [&](std::uint64_t line_no,
                std::uint64_t byte_offset,
                std::uint64_t byte_length,
                std::string_view content,
                const ExtractedFields&,
                bool has_crlf,
                bool is_truncated_eof) {
              if (collect_matches) {
                out_res.matches.push_back(detail::OwnedMatch{
                    .local_line_number = line_no,
                    .byte_offset = byte_offset,
                    .byte_length = byte_length,
                    .content = std::string(content),
                    .has_crlf = has_crlf,
                    .is_truncated_eof = is_truncated_eof,
                });
              }
              return true;
            });
      });
}

} // namespace logloom
