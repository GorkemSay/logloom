#pragma once

#include "logloom/line_reader.hpp"
#include "logloom/posix_file.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace logloom {

struct LineIndexOptions {
  std::size_t scan_chunk_size = 256 * 1024;
  std::size_t seek_chunk_size = 16 * 1024;
  std::size_t max_line_bytes = 16 * 1024 * 1024;
  // 16 MiB default checkpoint budget: dense (stride 1) up to 2,097,152 lines,
  // then automatically doubles stride to keep index memory bounded.
  std::size_t max_memory_bytes = 16 * 1024 * 1024;
  bool strip_utf8_bom = true;
};

struct IndexedLine {
  std::uint64_t line_number = 0;
  std::uint64_t byte_offset = 0;
  std::uint64_t byte_length = 0;
  std::string content{};
  bool has_crlf = false;
  bool is_truncated_eof = false;
  bool exceeds_max_length = false;
};

/// Adaptive byte-offset line index supporting O(1) seek random line & context reads.
class LineIndex {
public:
  [[nodiscard]] static std::optional<LineIndex> build(
      const PosixFile& file,
      std::error_code& ec,
      LineIndexOptions options = {});

  [[nodiscard]] std::uint64_t line_count() const noexcept;
  [[nodiscard]] std::uint64_t stride() const noexcept;
  [[nodiscard]] std::size_t checkpoint_count() const noexcept;
  [[nodiscard]] std::size_t memory_bytes() const noexcept;
  [[nodiscard]] const ReadStats& stats() const noexcept;

  [[nodiscard]] bool read_line(
      const PosixFile& file,
      std::uint64_t line_number,
      IndexedLine& out,
      std::error_code& ec) const;

  [[nodiscard]] bool read_context(
      const PosixFile& file,
      std::uint64_t center_line,
      std::uint64_t before,
      std::uint64_t after,
      std::vector<IndexedLine>& out,
      std::error_code& ec) const;

private:
  LineIndexOptions options_{};
  ReadStats stats_{};
  std::uint64_t stride_ = 1;
  std::vector<std::uint64_t> checkpoints_{};
};

} // namespace logloom
