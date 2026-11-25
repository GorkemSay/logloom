#pragma once

#include "logloom/posix_file.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace logloom {

struct LineReaderOptions {
  std::size_t chunk_size = 256 * 1024;
  std::size_t max_line_bytes = 16 * 1024 * 1024;
  bool strip_utf8_bom = true;
};

struct LineView {
  std::uint64_t line_number = 0;
  std::uint64_t byte_offset = 0;
  std::uint64_t byte_length = 0;
  std::string_view content{};
  bool has_crlf = false;
  bool is_truncated_eof = false;
  bool exceeds_max_length = false;
};

struct ReadStats {
  std::uint64_t total_lines = 0;
  std::uint64_t oversized_lines = 0;
  std::uint64_t bytes_read = 0;
  bool had_utf8_bom = false;
  bool has_truncated_final_line = false;
};

/// Streaming chunked pread line framer with bounded memory and exact byte-offset tracking.
class ChunkedLineReader {
public:
  explicit ChunkedLineReader(const PosixFile& file, LineReaderOptions options = {});

  ChunkedLineReader(const ChunkedLineReader&) = delete;
  ChunkedLineReader& operator=(const ChunkedLineReader&) = delete;

  ChunkedLineReader(ChunkedLineReader&&) noexcept = default;
  ChunkedLineReader& operator=(ChunkedLineReader&&) noexcept = default;

  /// Reads the next framed line into `out`. Returns false on EOF or error (check `ec`).
  /// `out.content` remains valid until the next call to `next_line()`.
  [[nodiscard]] bool next_line(LineView& out, std::error_code& ec);

  [[nodiscard]] const ReadStats& stats() const noexcept;

private:
  const PosixFile* file_ = nullptr;
  LineReaderOptions options_{};
  std::vector<char> chunk_buf_{};
  std::size_t chunk_pos_ = 0;
  std::size_t chunk_len_ = 0;
  std::uint64_t chunk_file_offset_ = 0;
  std::uint64_t file_offset_ = 0;
  bool initialized_ = false;
  bool eof_ = false;
  std::string line_buf_{};
  ReadStats stats_{};
};

} // namespace logloom
