#include "logloom/line_reader.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>

namespace logloom {

ChunkedLineReader::ChunkedLineReader(const PosixFile& file, LineReaderOptions options)
    : file_(&file),
      options_(options),
      chunk_buf_(std::max<std::size_t>(options.chunk_size, 1)) {}

const ReadStats& ChunkedLineReader::stats() const noexcept {
  return stats_;
}

bool ChunkedLineReader::next_line(LineView& out, std::error_code& ec) {
  ec.clear();
  out = LineView{};

  if (file_ == nullptr || !file_->is_open()) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    return false;
  }

  if (!initialized_) {
    initialized_ = true;
    if (options_.strip_utf8_bom) {
      std::array<char, 3> bom{};
      const std::size_t n = file_->pread_full(0, bom, ec);
      if (ec) {
        return false;
      }
      if (n == 3 &&
          static_cast<unsigned char>(bom[0]) == 0xEF &&
          static_cast<unsigned char>(bom[1]) == 0xBB &&
          static_cast<unsigned char>(bom[2]) == 0xBF) {
        stats_.had_utf8_bom = true;
        file_offset_ = 3;
        stats_.bytes_read = 3;
      }
    }
  }

  // Allow +1 byte in line_buf_ so a trailing '\r' on a max_line_bytes CRLF line
  // is preserved until '\n' confirms CRLF stripping.
  const std::uint64_t max_bytes = options_.max_line_bytes;
  const std::uint64_t buf_limit =
      max_bytes < std::numeric_limits<std::uint64_t>::max() ? max_bytes + 1ULL : max_bytes;

  bool accumulating = false;
  std::uint64_t line_start_offset = 0;
  std::uint64_t raw_len = 0;
  char last_byte = '\0';
  bool oversized = false;
  line_buf_.clear();

  while (true) {
    if (chunk_pos_ == chunk_len_) {
      if (eof_) {
        if (!accumulating) {
          return false;
        }

        const std::uint64_t payload_len = raw_len;
        if (payload_len > max_bytes) {
          oversized = true;
          line_buf_.clear();
        }

        out.line_number = ++stats_.total_lines;
        out.byte_offset = line_start_offset;
        out.byte_length = payload_len;
        out.has_crlf = false;
        out.is_truncated_eof = true;
        out.exceeds_max_length = oversized;
        out.content = oversized ? std::string_view{} : std::string_view(line_buf_);

        stats_.has_truncated_final_line = true;
        if (oversized) {
          ++stats_.oversized_lines;
        }
        return true;
      }

      const std::size_t n = file_->pread_full(file_offset_, chunk_buf_, ec);
      if (ec) {
        return false;
      }

      chunk_file_offset_ = file_offset_;
      file_offset_ += n;
      stats_.bytes_read += n;
      chunk_pos_ = 0;
      chunk_len_ = n;

      if (n == 0) {
        eof_ = true;
        continue;
      }
    }

    const char* avail_ptr = chunk_buf_.data() + chunk_pos_;
    const std::size_t avail_len = chunk_len_ - chunk_pos_;
    const void* nl_void = std::memchr(avail_ptr, '\n', avail_len);

    if (nl_void != nullptr) {
      const std::size_t seg_len =
          static_cast<std::size_t>(static_cast<const char*>(nl_void) - avail_ptr);

      if (!accumulating) {
        const bool has_crlf = (seg_len > 0 && avail_ptr[seg_len - 1] == '\r');
        const std::size_t payload_len = has_crlf ? (seg_len - 1) : seg_len;
        const std::uint64_t line_offset = chunk_file_offset_ + chunk_pos_;
        chunk_pos_ += seg_len + 1;

        const bool is_oversized = (payload_len > max_bytes);
        out.line_number = ++stats_.total_lines;
        out.byte_offset = line_offset;
        out.byte_length = payload_len;
        out.has_crlf = has_crlf;
        out.is_truncated_eof = false;
        out.exceeds_max_length = is_oversized;

        if (is_oversized) {
          ++stats_.oversized_lines;
          out.content = {};
        } else {
          out.content = std::string_view(avail_ptr, payload_len);
        }
        return true;
      }

      if (seg_len > 0) {
        last_byte = avail_ptr[seg_len - 1];
        if (!oversized) {
          if (seg_len <= buf_limit - raw_len) {
            line_buf_.append(avail_ptr, seg_len);
          } else {
            oversized = true;
            line_buf_.clear();
          }
        }
        raw_len += seg_len;
      }

      chunk_pos_ += seg_len + 1;

      const bool has_crlf = (raw_len > 0 && last_byte == '\r');
      const std::uint64_t payload_len = has_crlf ? (raw_len - 1) : raw_len;
      if (has_crlf && !oversized) {
        line_buf_.pop_back();
      }
      if (payload_len > max_bytes) {
        oversized = true;
        line_buf_.clear();
      }

      out.line_number = ++stats_.total_lines;
      out.byte_offset = line_start_offset;
      out.byte_length = payload_len;
      out.has_crlf = has_crlf;
      out.is_truncated_eof = false;
      out.exceeds_max_length = oversized;
      out.content = oversized ? std::string_view{} : std::string_view(line_buf_);

      if (oversized) {
        ++stats_.oversized_lines;
      }
      return true;
    }

    if (!accumulating) {
      accumulating = true;
      line_start_offset = chunk_file_offset_ + chunk_pos_;
    }

    last_byte = avail_ptr[avail_len - 1];
    if (!oversized) {
      if (avail_len <= buf_limit - raw_len) {
        line_buf_.append(avail_ptr, avail_len);
      } else {
        oversized = true;
        line_buf_.clear();
      }
    }
    raw_len += avail_len;
    chunk_pos_ = chunk_len_;
  }
}

} // namespace logloom
