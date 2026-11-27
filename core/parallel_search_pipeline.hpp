#pragma once

#include "search_match_internal.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace logloom::detail {

struct ByteRange {
  std::uint64_t start = 0;
  std::uint64_t end = 0;
};

[[nodiscard]] inline std::size_t resolve_worker_count(
    std::size_t requested,
    std::size_t partition_count) noexcept {
  std::size_t worker_count = requested;
  if (worker_count == 0) {
    const unsigned int hw = std::thread::hardware_concurrency();
    worker_count = hw > 0 ? static_cast<std::size_t>(hw) : 1U;
  }
  return std::min(worker_count, partition_count);
}

[[nodiscard]] inline bool is_cancelled(
    const std::atomic<bool>& internal_cancel,
    const std::atomic<bool>* external_cancel) noexcept {
  return internal_cancel.load(std::memory_order_acquire) ||
         (external_cancel != nullptr && external_cancel->load(std::memory_order_relaxed));
}

[[nodiscard]] inline bool emit_partition_matches(
    const PartitionResult& part_res,
    std::uint64_t line_offset,
    const SearchQuery& query,
    const JsonlExtractOptions& extract_opts,
    const MatchCallback& on_match,
    const std::atomic<bool>* cancel_flag,
    SearchStats& total_stats) {
  if (!on_match) {
    if (query.limit > 0) {
      const std::uint64_t remaining = query.limit - total_stats.matches_found;
      if (part_res.stats.matches_found >= remaining) {
        total_stats.matches_found = query.limit;
        total_stats.limit_reached = true;
        return true;
      }
    }
    total_stats.matches_found += part_res.stats.matches_found;
    return false;
  }

  for (const auto& owned : part_res.matches) {
    if (cancel_flag != nullptr && cancel_flag->load(std::memory_order_relaxed)) {
      total_stats.cancelled = true;
      return true;
    }

    ++total_stats.matches_found;
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
      return true;
    }

    if (query.limit > 0 && total_stats.matches_found >= query.limit) {
      total_stats.limit_reached = true;
      return true;
    }
  }

  return false;
}

template <typename ScanRangeFn>
[[nodiscard]] SearchStats run_parallel_partition_search(
    const std::vector<ByteRange>& partitions,
    const SearchQuery& query,
    const MatchCallback& on_match,
    std::size_t worker_count,
    std::error_code& ec,
    const std::atomic<bool>* cancel_flag,
    ScanRangeFn&& scan_range) {
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
      {
        std::lock_guard<std::mutex> lock(mu);
        --active_workers;
      }
      cv_done.notify_one();
      return;
    }
    std::string unescape_buf;

    while (true) {
      std::size_t part_idx = 0;
      {
        std::unique_lock<std::mutex> lock(mu);
        cv_work.wait(lock, [&] {
          return is_cancelled(internal_cancel, cancel_flag) ||
                 next_to_claim >= partitions.size() ||
                 (next_to_claim - next_to_emit) < max_in_flight;
        });
        if (is_cancelled(internal_cancel, cancel_flag) ||
            next_to_claim >= partitions.size()) {
          break;
        }
        part_idx = next_to_claim++;
      }

      PartitionResult part_res{};
      scan_range(partitions[part_idx], extract_opts, worker_regex, unescape_buf, internal_cancel, part_res);

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

    bool stop_emitting = emit_partition_matches(
        part_res, line_offset, query, extract_opts, on_match, cancel_flag, total_stats);

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

} // namespace logloom::detail
