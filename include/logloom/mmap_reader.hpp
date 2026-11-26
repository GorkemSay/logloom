#pragma once

#include "logloom/posix_file.hpp"
#include "logloom/search_engine.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace logloom {

/// Isolated read-only memory-mapped file wrapper for benchmarking against pread.
///
/// SAFETY NOTE: Unlike chunked pread, mmap exposes the process to SIGBUS if the
/// underlying file is truncated concurrently by another process, or if an I/O error
/// occurs while paging in mapped pages. Keep pread as the default safe backend.
class MmapFile {
public:
  MmapFile() noexcept = default;
  ~MmapFile();

  MmapFile(const MmapFile&) = delete;
  MmapFile& operator=(const MmapFile&) = delete;

  MmapFile(MmapFile&& other) noexcept;
  MmapFile& operator=(MmapFile&& other) noexcept;

  /// Maps `file` read-only with MAP_PRIVATE and hints MADV_SEQUENTIAL.
  /// Handles 0-byte files safely without calling mmap(2) with length 0.
  [[nodiscard]] static std::optional<MmapFile> map_read_only(
      const PosixFile& file,
      std::error_code& ec) noexcept;

  [[nodiscard]] bool is_mapped() const noexcept;
  [[nodiscard]] const char* data() const noexcept;
  [[nodiscard]] std::uint64_t size() const noexcept;
  [[nodiscard]] std::string_view view() const noexcept;

private:
  MmapFile(const char* data, std::uint64_t size, bool mapped) noexcept;
  void unmap() noexcept;

  const char* data_ = nullptr;
  std::uint64_t size_ = 0;
  bool mapped_ = false;
};

/// Searches a memory-mapped file using the same semantics and deterministic file
/// ordering as `search_file` / `search_file_parallel`.
[[nodiscard]] SearchStats search_file_mmap(
    const MmapFile& mapped,
    const SearchQuery& query,
    const MatchCallback& on_match,
    std::error_code& ec,
    ParallelSearchOptions parallel_options = {},
    const std::atomic<bool>* cancel_flag = nullptr);

} // namespace logloom
