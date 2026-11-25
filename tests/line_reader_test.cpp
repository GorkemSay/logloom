#include "logloom/line_reader.hpp"
#include "logloom/posix_file.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
            ("logloom_test_" + std::to_string(id) + ".bin");

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

struct OwnedLine {
  std::uint64_t line_number = 0;
  std::uint64_t byte_offset = 0;
  std::uint64_t byte_length = 0;
  std::string content{};
  bool has_crlf = false;
  bool is_truncated_eof = false;
  bool exceeds_max_length = false;

  bool operator==(const OwnedLine&) const = default;
};

std::vector<OwnedLine> read_all(
    const logloom::PosixFile& file,
    logloom::LineReaderOptions options,
    logloom::ReadStats* out_stats = nullptr) {
  logloom::ChunkedLineReader reader(file, options);
  std::vector<OwnedLine> lines;
  logloom::LineView view{};
  std::error_code ec;
  while (reader.next_line(view, ec)) {
    REQUIRE_FALSE(ec);
    lines.push_back(OwnedLine{
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
  if (out_stats != nullptr) {
    *out_stats = reader.stats();
  }
  return lines;
}

void verify_offsets_round_trip(
    const logloom::PosixFile& file,
    const std::vector<OwnedLine>& lines) {
  for (const auto& line : lines) {
    if (line.exceeds_max_length) {
      continue;
    }
    std::string buf(static_cast<std::size_t>(line.byte_length), '\0');
    std::error_code ec;
    const std::size_t n = file.pread_full(line.byte_offset, buf, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(n == line.byte_length);
    REQUIRE(buf == line.content);
  }
}

} // namespace

TEST_CASE("PosixFile handles errors, regular files, pread_full, and moves", "[posix_file]") {
  std::error_code ec;

  SECTION("non-existent file fails cleanly") {
    const auto missing = std::filesystem::temp_directory_path() / "logloom_does_not_exist_12345.log";
    auto f = logloom::PosixFile::open_read_only(missing, ec);
    REQUIRE_FALSE(f.has_value());
    REQUIRE(ec == std::errc::no_such_file_or_directory);
  }

  SECTION("directory fails with is_a_directory") {
    auto f = logloom::PosixFile::open_read_only(std::filesystem::temp_directory_path(), ec);
    REQUIRE_FALSE(f.has_value());
    REQUIRE(ec == std::errc::is_a_directory);
  }

  SECTION("open, pread_full at offset and past EOF, and move semantics") {
    TempFile tmp("0123456789");
    auto opt = logloom::PosixFile::open_read_only(tmp.path(), ec);
    REQUIRE(opt.has_value());
    REQUIRE_FALSE(ec);

    logloom::PosixFile file = std::move(*opt);
    REQUIRE(file.is_open());
    REQUIRE(file.fd() >= 0);
    REQUIRE(file.size() == 10);

    std::array<char, 4> slice{};
    REQUIRE(file.pread_full(3, slice, ec) == 4);
    REQUIRE_FALSE(ec);
    REQUIRE(std::string_view(slice.data(), slice.size()) == "3456");

    std::array<char, 8> tail{};
    REQUIRE(file.pread_full(7, tail, ec) == 3);
    REQUIRE_FALSE(ec);
    REQUIRE(std::string_view(tail.data(), 3) == "789");

    REQUIRE(file.pread_full(100, tail, ec) == 0);
    REQUIRE_FALSE(ec);

    logloom::PosixFile moved(std::move(file));
    REQUIRE(moved.is_open());
    REQUIRE(moved.size() == 10);
    REQUIRE_FALSE(file.is_open());

    logloom::PosixFile moved_assign;
    moved_assign = std::move(moved);
    REQUIRE(moved_assign.is_open());
    REQUIRE_FALSE(moved.is_open());
  }
}

TEST_CASE("ChunkedLineReader handles empty and 1-byte files", "[line_reader][edge]") {
  std::error_code ec;

  SECTION("empty 0-byte file") {
    TempFile tmp("");
    auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
    REQUIRE(file.has_value());

    logloom::ReadStats stats{};
    const auto lines = read_all(*file, {}, &stats);
    REQUIRE(lines.empty());
    REQUIRE(stats.total_lines == 0);
    REQUIRE_FALSE(stats.had_utf8_bom);
    REQUIRE_FALSE(stats.has_truncated_final_line);
  }

  SECTION("1-byte file without newline") {
    TempFile tmp("x");
    auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
    REQUIRE(file.has_value());

    logloom::ReadStats stats{};
    const auto lines = read_all(*file, {.chunk_size = 1}, &stats);
    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == OwnedLine{1, 0, 1, "x", false, true, false});
    REQUIRE(stats.total_lines == 1);
    REQUIRE(stats.has_truncated_final_line);
    verify_offsets_round_trip(*file, lines);
  }

  SECTION("1-byte newline-only file") {
    TempFile tmp("\n");
    auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
    REQUIRE(file.has_value());

    logloom::ReadStats stats{};
    const auto lines = read_all(*file, {.chunk_size = 1}, &stats);
    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == OwnedLine{1, 0, 0, "", false, false, false});
    REQUIRE(stats.total_lines == 1);
    REQUIRE_FALSE(stats.has_truncated_final_line);
    verify_offsets_round_trip(*file, lines);
  }
}

TEST_CASE("ChunkedLineReader tracks exact offsets, CRLF, and empty lines", "[line_reader]") {
  const std::string data = "alpha\n\nbeta\r\ngamma\r\r\ndelta\r";
  TempFile tmp(data);
  std::error_code ec;
  auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
  REQUIRE(file.has_value());

  logloom::ReadStats stats{};
  const auto lines = read_all(*file, {.chunk_size = 64}, &stats);

  REQUIRE(lines.size() == 5);
  REQUIRE(lines[0] == OwnedLine{1, 0, 5, "alpha", false, false, false});
  REQUIRE(lines[1] == OwnedLine{2, 6, 0, "", false, false, false});
  REQUIRE(lines[2] == OwnedLine{3, 7, 4, "beta", true, false, false});
  REQUIRE(lines[3] == OwnedLine{4, 13, 6, "gamma\r", true, false, false});
  REQUIRE(lines[4] == OwnedLine{5, 21, 6, "delta\r", false, true, false});
  REQUIRE(stats.total_lines == 5);
  REQUIRE(stats.has_truncated_final_line);
  verify_offsets_round_trip(*file, lines);
}

TEST_CASE("ChunkedLineReader produces identical results across chunk boundaries", "[line_reader][boundary]") {
  // Includes LF at chunk boundaries, CRLF split across chunks, empty lines,
  // multi-chunk line, and truncated final line.
  const std::string data =
      "0123\r\n"
      "\n"
      "\r\n"
      "abcdefghij1234567890\n"
      "{\"request_id\":\"req-1\",\"msg\":\"ok\"}\r\n"
      "{\"request_id\":\"req-2\",\"partial\":true";

  TempFile tmp(data);
  std::error_code ec;
  auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
  REQUIRE(file.has_value());

  const auto baseline = read_all(*file, {.chunk_size = 4096});
  REQUIRE(baseline.size() == 6);
  verify_offsets_round_trip(*file, baseline);

  for (const std::size_t chunk_sz : {1, 2, 3, 4, 5, 7, 8, 16, 64}) {
    CAPTURE(chunk_sz);
    logloom::ReadStats stats{};
    const auto actual = read_all(*file, {.chunk_size = chunk_sz}, &stats);
    REQUIRE(actual == baseline);
    REQUIRE(stats.total_lines == 6);
    REQUIRE(stats.has_truncated_final_line);
  }
}

TEST_CASE("ChunkedLineReader handles UTF-8 BOM across chunk sizes", "[line_reader][bom]") {
  std::error_code ec;
  const std::string bom = "\xEF\xBB\xBF";

  SECTION("BOM-only file produces zero lines when stripped") {
    TempFile tmp(bom);
    auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
    REQUIRE(file.has_value());

    logloom::ReadStats stats{};
    const auto lines = read_all(*file, {.chunk_size = 1}, &stats);
    REQUIRE(lines.empty());
    REQUIRE(stats.had_utf8_bom);
    REQUIRE_FALSE(stats.has_truncated_final_line);
  }

  SECTION("BOM + content stripped consistently across chunk sizes") {
    TempFile tmp(bom + "first\r\nsecond\n");
    auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
    REQUIRE(file.has_value());

    for (const std::size_t chunk_sz : {1, 2, 3, 64}) {
      CAPTURE(chunk_sz);
      logloom::ReadStats stats{};
      const auto lines = read_all(*file, {.chunk_size = chunk_sz}, &stats);
      REQUIRE(stats.had_utf8_bom);
      REQUIRE(lines.size() == 2);
      REQUIRE(lines[0] == OwnedLine{1, 3, 5, "first", true, false, false});
      REQUIRE(lines[1] == OwnedLine{2, 10, 6, "second", false, false, false});
      verify_offsets_round_trip(*file, lines);
    }

    logloom::ReadStats raw_stats{};
    const auto raw_lines =
        read_all(*file, {.chunk_size = 4, .strip_utf8_bom = false}, &raw_stats);
    REQUIRE_FALSE(raw_stats.had_utf8_bom);
    REQUIRE(raw_lines[0] == OwnedLine{1, 0, 8, bom + "first", true, false, false});
  }

  SECTION("partial BOM-like prefixes are preserved as data") {
    for (const std::string_view prefix : {std::string_view("\xEF\xBB"), std::string_view("\xEF")}) {
      TempFile tmp(std::string(prefix) + "abc\n");
      auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
      REQUIRE(file.has_value());

      logloom::ReadStats stats{};
      const auto lines = read_all(*file, {.chunk_size = 1}, &stats);
      REQUIRE_FALSE(stats.had_utf8_bom);
      REQUIRE(lines.size() == 1);
      REQUIRE(lines[0].byte_offset == 0);
      REQUIRE(lines[0].content == std::string(prefix) + "abc");
    }
  }
}

TEST_CASE("ChunkedLineReader enforces max_line_bytes safely without OOM", "[line_reader][cap]") {
  std::error_code ec;

  SECTION("exact cap with LF and CRLF is accepted, cap+1 is flagged oversized") {
    const std::string exact_5(5, 'a');
    const std::string over_6(6, 'b');
    const std::string data =
        exact_5 + "\n" +
        exact_5 + "\r\n" +
        over_6 + "\n" +
        over_6 + "\r\n" +
        "ok\n" +
        over_6; // truncated EOF and oversized

    TempFile tmp(data);
    auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
    REQUIRE(file.has_value());

    for (const std::size_t chunk_sz : {1, 3, 4, 8, 64}) {
      CAPTURE(chunk_sz);
      logloom::ReadStats stats{};
      const auto lines =
          read_all(*file, {.chunk_size = chunk_sz, .max_line_bytes = 5}, &stats);

      REQUIRE(lines.size() == 6);
      REQUIRE(lines[0] == OwnedLine{1, 0, 5, exact_5, false, false, false});
      REQUIRE(lines[1] == OwnedLine{2, 6, 5, exact_5, true, false, false});
      REQUIRE(lines[2] == OwnedLine{3, 13, 6, "", false, false, true});
      REQUIRE(lines[3] == OwnedLine{4, 20, 6, "", true, false, true});
      REQUIRE(lines[4] == OwnedLine{5, 28, 2, "ok", false, false, false});
      REQUIRE(lines[5] == OwnedLine{6, 31, 6, "", false, true, true});

      REQUIRE(stats.total_lines == 6);
      REQUIRE(stats.oversized_lines == 3);
      REQUIRE(stats.has_truncated_final_line);
    }
  }

  SECTION("max_line_bytes == 0 accepts empty lines and flags non-empty lines") {
    TempFile tmp("\n\r\na\n");
    auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
    REQUIRE(file.has_value());

    for (const std::size_t chunk_sz : {1, 4}) {
      CAPTURE(chunk_sz);
      logloom::ReadStats stats{};
      const auto lines =
          read_all(*file, {.chunk_size = chunk_sz, .max_line_bytes = 0}, &stats);
      REQUIRE(lines.size() == 3);
      REQUIRE(lines[0] == OwnedLine{1, 0, 0, "", false, false, false});
      REQUIRE(lines[1] == OwnedLine{2, 1, 0, "", true, false, false});
      REQUIRE(lines[2] == OwnedLine{3, 3, 1, "", false, false, true});
      REQUIRE(stats.oversized_lines == 1);
    }
  }
}

TEST_CASE("ChunkedLineReader survives invalid UTF-8, embedded NUL, and malformed JSONL", "[line_reader][hostile]") {
  const std::string bad_utf8 = "\xFF\xFE\x80\xC0\xAF";
  const std::string with_nul = std::string("head\0tail", 9);
  const std::string truncated_json = "{\"ts\":\"2026-11-25T10:00:00Z\",\"msg\":\"mid";
  const std::string data =
      bad_utf8 + "\n" +
      with_nul + "\r\n" +
      "plain log line\n" +
      truncated_json;

  TempFile tmp(data);
  std::error_code ec;
  auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
  REQUIRE(file.has_value());

  for (const std::size_t chunk_sz : {2, 5, 64}) {
    CAPTURE(chunk_sz);
    logloom::ReadStats stats{};
    const auto lines = read_all(*file, {.chunk_size = chunk_sz}, &stats);
    REQUIRE(lines.size() == 4);
    REQUIRE(lines[0].content == bad_utf8);
    REQUIRE(lines[1].content == with_nul);
    REQUIRE(lines[1].has_crlf);
    REQUIRE(lines[2].content == "plain log line");
    REQUIRE(lines[3].content == truncated_json);
    REQUIRE(lines[3].is_truncated_eof);
    REQUIRE(stats.has_truncated_final_line);
    verify_offsets_round_trip(*file, lines);
  }
}
