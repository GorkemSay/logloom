#include "logloom/line_index.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

class TempFile {
public:
  explicit TempFile(std::string_view bytes) {
    static std::atomic<std::uint64_t> counter{0};
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    path_ = std::filesystem::temp_directory_path() /
            ("logloom_index_test_" + std::to_string(id) + ".bin");

    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    REQUIRE(out.is_open());
    if (!bytes.empty()) {
      out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    out.flush();
    REQUIRE(out.good());
  }

  ~TempFile() {
    std::error_code ec;
    std::filesystem::remove(path_, ec);
  }

  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;

  const std::filesystem::path& path() const noexcept { return path_; }

private:
  std::filesystem::path path_;
};

void require_same_line(const logloom::IndexedLine& actual, const logloom::IndexedLine& expected) {
  REQUIRE(actual.line_number == expected.line_number);
  REQUIRE(actual.byte_offset == expected.byte_offset);
  REQUIRE(actual.byte_length == expected.byte_length);
  REQUIRE(actual.content == expected.content);
  REQUIRE(actual.has_crlf == expected.has_crlf);
  REQUIRE(actual.is_truncated_eof == expected.is_truncated_eof);
  REQUIRE(actual.exceeds_max_length == expected.exceeds_max_length);
}

std::vector<logloom::IndexedLine> scan_ground_truth(
    const logloom::PosixFile& file,
    std::size_t max_line_bytes = 16 * 1024 * 1024) {
  logloom::ChunkedLineReader reader(
      file,
      {.chunk_size = 64, .max_line_bytes = max_line_bytes, .strip_utf8_bom = true});
  std::vector<logloom::IndexedLine> expected;
  logloom::LineView view{};
  std::error_code ec;
  while (reader.next_line(view, ec)) {
    REQUIRE_FALSE(ec);
    expected.push_back(logloom::IndexedLine{
        .line_number = view.line_number,
        .byte_offset = view.byte_offset,
        .byte_length = view.byte_length,
        .content = std::string(view.content),
        .has_crlf = view.has_crlf,
        .is_truncated_eof = view.is_truncated_eof,
        .exceeds_max_length = view.exceeds_max_length,
    });
  }
  REQUIRE_FALSE(ec);
  return expected;
}

} // namespace

TEST_CASE("LineIndex handles empty and BOM-only files gracefully", "[line_index][edge]") {
  std::error_code ec;

  for (const std::string_view data : {std::string_view(""), std::string_view("\xEF\xBB\xBF")}) {
    TempFile tmp(data);
    auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
    REQUIRE(file.has_value());

    auto idx = logloom::LineIndex::build(*file, ec);
    REQUIRE(idx.has_value());
    REQUIRE_FALSE(ec);
    REQUIRE(idx->line_count() == 0);
    REQUIRE(idx->checkpoint_count() == 0);

    logloom::IndexedLine line{};
    REQUIRE_FALSE(idx->read_line(*file, 0, line, ec));
    REQUIRE_FALSE(ec);
    REQUIRE_FALSE(idx->read_line(*file, 1, line, ec));
    REQUIRE_FALSE(ec);

    std::vector<logloom::IndexedLine> ctx;
    REQUIRE_FALSE(idx->read_context(*file, 1, 5, 5, ctx, ec));
    REQUIRE_FALSE(ec);
    REQUIRE(ctx.empty());
  }
}

TEST_CASE("LineIndex dense read_line matches ground truth with BOM, CRLF, empty lines, and EOF", "[line_index]") {
  const std::string data =
      "\xEF\xBB\xBF"
      "first\n"
      "\r\n"
      "third-crlf\r\n"
      "\n"
      "truncated-final";

  TempFile tmp(data);
  std::error_code ec;
  auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
  REQUIRE(file.has_value());

  const auto expected = scan_ground_truth(*file);
  REQUIRE(expected.size() == 5);

  auto idx = logloom::LineIndex::build(*file, ec, {.scan_chunk_size = 4, .seek_chunk_size = 3});
  REQUIRE(idx.has_value());
  REQUIRE_FALSE(ec);
  REQUIRE(idx->line_count() == 5);
  REQUIRE(idx->stride() == 1);
  REQUIRE(idx->stats().had_utf8_bom);
  REQUIRE(idx->stats().has_truncated_final_line);

  for (std::uint64_t n = idx->line_count(); n >= 1; --n) {
    logloom::IndexedLine actual{};
    REQUIRE(idx->read_line(*file, n, actual, ec));
    REQUIRE_FALSE(ec);
    require_same_line(actual, expected[static_cast<std::size_t>(n - 1)]);
  }

  logloom::IndexedLine out_of_range{};
  REQUIRE_FALSE(idx->read_line(*file, 0, out_of_range, ec));
  REQUIRE_FALSE(ec);
  REQUIRE_FALSE(idx->read_line(*file, 6, out_of_range, ec));
  REQUIRE_FALSE(ec);
}

TEST_CASE("LineIndex adaptive strided checkpoints stay within memory budget and read accurately", "[line_index][sparse]") {
  std::string data = "\xEF\xBB\xBF";
  for (int i = 1; i <= 199; ++i) {
    if (i % 15 == 0) {
      data += (i % 30 == 0) ? "\r\n" : "\n";
    } else {
      data += "line-" + std::to_string(i) + (i % 2 == 0 ? "\r\n" : "\n");
    }
  }
  data += "line-200-truncated";

  TempFile tmp(data);
  std::error_code ec;
  auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
  REQUIRE(file.has_value());

  const auto expected = scan_ground_truth(*file);
  REQUIRE(expected.size() == 200);

  for (const std::size_t budget : {16U, 24U, 32U, 64U}) {
    CAPTURE(budget);
    auto idx = logloom::LineIndex::build(*file, ec, {
        .scan_chunk_size = 17,
        .seek_chunk_size = 13,
        .max_memory_bytes = budget,
    });
    REQUIRE(idx.has_value());
    REQUIRE_FALSE(ec);
    REQUIRE(idx->line_count() == 200);
    REQUIRE(idx->stride() > 1);
    REQUIRE(idx->memory_bytes() <= budget);

    for (std::uint64_t n = 1; n <= 200; ++n) {
      logloom::IndexedLine actual{};
      REQUIRE(idx->read_line(*file, n, actual, ec));
      REQUIRE_FALSE(ec);
      require_same_line(actual, expected[static_cast<std::size_t>(n - 1)]);
    }
  }
}

TEST_CASE("LineIndex read_context handles windowing, clamping, oversized lines, and strided indexes", "[line_index][context]") {
  std::string data;
  for (int i = 1; i <= 25; ++i) {
    if (i == 12) {
      data += std::string(20, 'X') + "\r\n"; // oversized when max_line_bytes = 12
    } else {
      data += "row-" + std::to_string(i) + (i == 25 ? "" : (i % 2 == 0 ? "\r\n" : "\n"));
    }
  }

  TempFile tmp(data);
  std::error_code ec;
  auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
  REQUIRE(file.has_value());

  const auto expected = scan_ground_truth(*file, 12);
  REQUIRE(expected.size() == 25);

  auto dense = logloom::LineIndex::build(*file, ec, {
      .scan_chunk_size = 11,
      .seek_chunk_size = 7,
      .max_line_bytes = 12,
  });
  auto sparse = logloom::LineIndex::build(*file, ec, {
      .scan_chunk_size = 11,
      .seek_chunk_size = 7,
      .max_line_bytes = 12,
      .max_memory_bytes = 24,
  });
  REQUIRE(dense.has_value());
  REQUIRE(sparse.has_value());
  REQUIRE(sparse->stride() > 1);

  for (const auto* idx : {&*dense, &*sparse}) {
    std::vector<logloom::IndexedLine> ctx;

    SECTION("middle window spanning oversized line") {
      REQUIRE(idx->read_context(*file, 12, 2, 2, ctx, ec));
      REQUIRE_FALSE(ec);
      REQUIRE(ctx.size() == 5);
      for (std::size_t i = 0; i < ctx.size(); ++i) {
        require_same_line(ctx[i], expected[9 + i]);
      }
      REQUIRE(ctx[2].exceeds_max_length);
      REQUIRE(ctx[2].byte_length == 20);
    }

    SECTION("clamping at start, end, and UINT64_MAX overflow guard") {
      REQUIRE(idx->read_context(*file, 2, 100, 1, ctx, ec));
      REQUIRE_FALSE(ec);
      REQUIRE(ctx.size() == 3);
      REQUIRE(ctx.front().line_number == 1);
      REQUIRE(ctx.back().line_number == 3);

      REQUIRE(idx->read_context(*file, 24, 1, 100, ctx, ec));
      REQUIRE_FALSE(ec);
      REQUIRE(ctx.size() == 3);
      REQUIRE(ctx.front().line_number == 23);
      REQUIRE(ctx.back().line_number == 25);
      REQUIRE(ctx.back().is_truncated_eof);

      const auto max_u64 = std::numeric_limits<std::uint64_t>::max();
      REQUIRE(idx->read_context(*file, 10, max_u64, max_u64, ctx, ec));
      REQUIRE_FALSE(ec);
      REQUIRE(ctx.size() == 25);
    }

    SECTION("zero before and zero after returns single line") {
      REQUIRE(idx->read_context(*file, 7, 0, 0, ctx, ec));
      REQUIRE_FALSE(ec);
      REQUIRE(ctx.size() == 1);
      require_same_line(ctx[0], expected[6]);
    }

    SECTION("out-of-range center_line returns false without error") {
      REQUIRE_FALSE(idx->read_context(*file, 0, 2, 2, ctx, ec));
      REQUIRE_FALSE(ec);
      REQUIRE_FALSE(idx->read_context(*file, 26, 2, 2, ctx, ec));
      REQUIRE_FALSE(ec);
    }
  }
}
