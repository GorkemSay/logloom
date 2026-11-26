#include "logloom/line_index.hpp"

#include <algorithm>
#include <optional>

namespace logloom {
namespace {

IndexedLine to_indexed_line(const LineView& view, std::uint64_t line_number) {
  return IndexedLine{
      .line_number = line_number,
      .byte_offset = view.byte_offset,
      .byte_length = view.byte_length,
      .content = std::string(view.content),
      .has_crlf = view.has_crlf,
      .is_truncated_eof = view.is_truncated_eof,
      .exceeds_max_length = view.exceeds_max_length,
  };
}

[[nodiscard]] bool seek_to_line(
    const PosixFile& file,
    const std::vector<std::uint64_t>& checkpoints,
    std::uint64_t stride,
    const LineIndexOptions& options,
    std::uint64_t target_line,
    std::optional<ChunkedLineReader>& reader,
    LineView& view,
    std::error_code& ec) {
  const std::uint64_t zero_idx = target_line - 1;
  const std::uint64_t cp_idx = zero_idx / stride;
  const std::uint64_t cp_offset = checkpoints[static_cast<std::size_t>(cp_idx)];
  const std::uint64_t skip = zero_idx - (cp_idx * stride);

  reader.emplace(
      file,
      LineReaderOptions{
          .chunk_size = options.seek_chunk_size,
          .max_line_bytes = options.max_line_bytes,
          .strip_utf8_bom = false,
          .start_offset = cp_offset,
      });

  for (std::uint64_t i = 0; i <= skip; ++i) {
    if (!reader->next_line(view, ec)) {
      return false;
    }
  }
  return true;
}

} // namespace

std::optional<LineIndex> LineIndex::build(
    const PosixFile& file,
    std::error_code& ec,
    LineIndexOptions options,
    const IndexProgressCallback& progress_cb,
    const std::atomic<bool>* cancel_flag) {
  ec.clear();

  if (!file.is_open()) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    return std::nullopt;
  }

  if (cancel_flag != nullptr && cancel_flag->load(std::memory_order_relaxed)) {
    ec = std::make_error_code(std::errc::operation_canceled);
    return std::nullopt;
  }

  const std::uint64_t total_bytes = file.size();
  const std::size_t max_checkpoints =
      std::max<std::size_t>(options.max_memory_bytes / sizeof(std::uint64_t), 2);

  LineIndex index;
  index.options_ = options;
  index.stride_ = 1;

  ChunkedLineReader reader(
      file,
      LineReaderOptions{
          .chunk_size = options.scan_chunk_size,
          .max_line_bytes = options.max_line_bytes,
          .strip_utf8_bom = options.strip_utf8_bom,
          .start_offset = 0,
      });

  LineView view{};
  while (reader.next_line(view, ec)) {
    if (cancel_flag != nullptr && cancel_flag->load(std::memory_order_relaxed)) {
      ec = std::make_error_code(std::errc::operation_canceled);
      return std::nullopt;
    }

    const std::uint64_t zero_idx = view.line_number - 1;
    if (zero_idx % index.stride_ == 0) {
      if (index.checkpoints_.size() >= max_checkpoints) {
        std::size_t write_idx = 0;
        for (std::size_t i = 0; i < index.checkpoints_.size(); i += 2) {
          index.checkpoints_[write_idx++] = index.checkpoints_[i];
        }
        index.checkpoints_.resize(write_idx);
        index.stride_ *= 2;
      }
      if (zero_idx % index.stride_ == 0) {
        if (index.checkpoints_.size() == index.checkpoints_.capacity() &&
            index.checkpoints_.capacity() < max_checkpoints) {
          const std::size_t cur_cap = index.checkpoints_.capacity();
          const std::size_t grown = (cur_cap == 0) ? 1024U : (cur_cap * 2U);
          index.checkpoints_.reserve(std::min(grown, max_checkpoints));
        }
        index.checkpoints_.push_back(view.byte_offset);
      }
    }

    if (progress_cb && (view.line_number % 4096ULL) == 0ULL) {
      const std::uint64_t processed = std::min(reader.stats().bytes_read, total_bytes);
      if (!progress_cb(processed, total_bytes, view.line_number)) {
        ec = std::make_error_code(std::errc::operation_canceled);
        return std::nullopt;
      }
    }
  }

  if (ec) {
    return std::nullopt;
  }

  index.stats_ = reader.stats();
  if (progress_cb &&
      !progress_cb(total_bytes, total_bytes, index.stats_.total_lines)) {
    ec = std::make_error_code(std::errc::operation_canceled);
    return std::nullopt;
  }

  return index;
}

std::uint64_t LineIndex::line_count() const noexcept {
  return stats_.total_lines;
}

std::uint64_t LineIndex::stride() const noexcept {
  return stride_;
}

std::size_t LineIndex::checkpoint_count() const noexcept {
  return checkpoints_.size();
}

std::size_t LineIndex::memory_bytes() const noexcept {
  return checkpoints_.size() * sizeof(std::uint64_t);
}

const ReadStats& LineIndex::stats() const noexcept {
  return stats_;
}

bool LineIndex::read_line(
    const PosixFile& file,
    std::uint64_t line_number,
    IndexedLine& out,
    std::error_code& ec) const {
  ec.clear();
  out = IndexedLine{};

  if (!file.is_open()) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    return false;
  }

  if (line_number == 0 || line_number > stats_.total_lines) {
    return false;
  }

  std::optional<ChunkedLineReader> reader;
  LineView view{};
  if (!seek_to_line(file, checkpoints_, stride_, options_, line_number, reader, view, ec)) {
    return false;
  }

  out = to_indexed_line(view, line_number);
  return true;
}

bool LineIndex::read_context(
    const PosixFile& file,
    std::uint64_t center_line,
    std::uint64_t before,
    std::uint64_t after,
    std::vector<IndexedLine>& out,
    std::error_code& ec) const {
  ec.clear();
  out.clear();

  if (!file.is_open()) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    return false;
  }

  if (stats_.total_lines == 0 || center_line == 0 || center_line > stats_.total_lines) {
    return false;
  }

  const std::uint64_t first_line =
      (before >= center_line - 1) ? 1ULL : (center_line - before);
  const std::uint64_t last_line =
      (after >= stats_.total_lines - center_line) ? stats_.total_lines : (center_line + after);
  const std::uint64_t count = (last_line - first_line) + 1ULL;

  std::optional<ChunkedLineReader> reader;
  LineView view{};
  if (!seek_to_line(file, checkpoints_, stride_, options_, first_line, reader, view, ec)) {
    return false;
  }

  out.reserve(static_cast<std::size_t>(count));
  out.push_back(to_indexed_line(view, first_line));

  for (std::uint64_t offset = 1; offset < count; ++offset) {
    if (!reader->next_line(view, ec)) {
      out.clear();
      return false;
    }
    out.push_back(to_indexed_line(view, first_line + offset));
  }

  return true;
}

} // namespace logloom
