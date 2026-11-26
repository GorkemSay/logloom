#include "logloom/mmap_reader.hpp"
#include "search_match_internal.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <sys/mman.h>
#include <utility>
#include <vector>

namespace logloom {
namespace {

struct MmapPartition {
  std::size_t start = 0;
  std::size_t end = 0;
};

[[nodiscard]] std::vector<MmapPartition> partition_mapped_on_newlines(
    std::string_view view,
    std::size_t effective_start,
    std::size_t partition_bytes) {
  std::vector<MmapPartition> partitions;
  const std::size_t total = view.size();
  if (effective_start >= total) {
    return partitions;
  }

  const std::size_t part_size = std::max<std::size_t>(partition_bytes, 1);
  std::size_t cur = effective_start;

  while (cur < total) {
    if (total - cur <= part_size) {
      partitions.push_back(MmapPartition{cur, total});
      break;
    }

    const std::size_t scan_pos = cur + part_size - 1;
    const void* nl = std::memchr(view.data() + scan_pos, '\n', total - scan_pos);
    if (nl == nullptr) {
      partitions.push_back(MmapPartition{cur, total});
      break;
    }

    const auto nl_idx = static_cast<std::size_t>(static_cast<const char*>(nl) - view.data());
    const std::size_t part_end = nl_idx + 1;
    partitions.push_back(MmapPartition{cur, part_end});
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

    std::size_t raw_len = 0;
    bool is_truncated_eof = false;
    std::size_t next_pos = 0;

    if (nl != nullptr) {
      raw_len = static_cast<std::size_t>(static_cast<const char*>(nl) - (base + pos));
      next_pos = pos + raw_len + 1;
    } else {
      raw_len = avail;
      is_truncated_eof = true;
      next_pos = range_end;
    }

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
  std::size_t effective_start = 0;
  if (query.reader_options.strip_utf8_bom && full_view.size() >= 3 &&
      static_cast<unsigned char>(full_view[0]) == 0xEF &&
      static_cast<unsigned char>(full_view[1]) == 0xBB &&
      static_cast<unsigned char>(full_view[2]) == 0xBF) {
    effective_start = 3;
  }

  if (effective_start >= full_view.size()) {
    return SearchStats{};
  }

  const JsonlExtractOptions extract_opts{
      .trace_key = query.trace_key,
      .timestamp_key = query.timestamp_key,
      .level_key = query.level_key,
  };

  std::size_t worker_count = parallel_options.num_threads;
  if (worker_count == 0) {
    const unsigned int hw = std::thread::hardware_concurrency();
    worker_count = hw > 0 ? static_cast<std::size_t>(hw) : 1U;
  }

  const auto partitions = partition_mapped_on_newlines(
      full_view, effective_start, parallel_options.partition_bytes);
  if (partitions.empty()) {
    return SearchStats{};
  }
  worker_count = std::min(worker_count, partitions.size());

  if (worker_count <= 1 || partitions.size() <= 1) {
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

  compiled_regex.reset();
  const std::size_t max_in_flight = std::max<std::size_t>(worker_count * 2, 2);
  std::mutex mu;
  std::condition_variable cv_work;
  std::condition_variable cv_done;
  std::size_t next_to_claim = 0;
  std::size_t next_to_emit = 0;
  std::size_t active_workers = worker_count;
  std::vector<std::optional<detail::PartitionResult>> completed(partitions.size());
  std::atomic<bool> internal_cancel{false};
  std::vector<std::thread> threads;
  threads.reserve(worker_count);

  detail::ThreadPoolGuard pool_guard{internal_cancel, cv_work, threads};

  auto worker_fn = [&]() {
    std::optional<std::regex> worker_regex;
    std::error_code regex_ec;
    if (!detail::compile_query_regex(query, worker_regex, regex_ec)) {
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

      const MmapPartition part = partitions[part_idx];
      detail::PartitionResult part_res{};

      scan_mapped_range(
          full_view,
          part.start,
          part.end,
          query,
          extract_opts,
          worker_regex,
          unescape_buf,
          part_res.stats,
          cancel_flag,
          &internal_cancel,
          [&](std::uint64_t line_no,
              std::uint64_t byte_offset,
              std::uint64_t byte_length,
              std::string_view content,
              const ExtractedFields&,
              bool has_crlf,
              bool is_truncated_eof) {
            part_res.matches.push_back(detail::OwnedMatch{
                .local_line_number = line_no,
                .byte_offset = byte_offset,
                .byte_length = byte_length,
                .content = std::string(content),
                .has_crlf = has_crlf,
                .is_truncated_eof = is_truncated_eof,
            });
            return true;
          });

      {
        std::lock_guard<std::mutex> lock(mu);
        completed[part_idx] = std::move(part_res);
      }
      cv_done.notify_one();
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
    detail::PartitionResult part_res{};
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

    detail::merge_partition_stats(part_res.stats, total_stats);

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
