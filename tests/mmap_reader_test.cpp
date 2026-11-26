#include "logloom/mmap_reader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
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
            ("logloom_mmap_test_" + std::to_string(id) + ".jsonl");

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

struct OwnedMmapMatch {
  std::uint64_t line_number = 0;
  std::uint64_t byte_offset = 0;
  std::uint64_t byte_length = 0;
  std::string content{};
  std::optional<std::string> timestamp{};
  std::optional<std::string> level{};
  std::optional<std::string> trace_id{};
  bool has_crlf = false;
  bool is_truncated_eof = false;

  bool operator==(const OwnedMmapMatch&) const = default;
};

OwnedMmapMatch capture_match(const logloom::SearchMatch& m) {
  return OwnedMmapMatch{
      .line_number = m.line_number,
      .byte_offset = m.byte_offset,
      .byte_length = m.byte_length,
      .content = std::string(m.content),
      .timestamp = m.fields.timestamp ? std::optional<std::string>(*m.fields.timestamp)
                                      : std::nullopt,
      .level = m.fields.level ? std::optional<std::string>(*m.fields.level) : std::nullopt,
      .trace_id = m.fields.trace_id ? std::optional<std::string>(*m.fields.trace_id)
                                    : std::nullopt,
      .has_crlf = m.has_crlf,
      .is_truncated_eof = m.is_truncated_eof,
  };
}

void require_same_stats(const logloom::SearchStats& a, const logloom::SearchStats& b) {
  REQUIRE(a.lines_scanned == b.lines_scanned);
  REQUIRE(a.matches_found == b.matches_found);
  REQUIRE(a.json_lines == b.json_lines);
  REQUIRE(a.plain_text_lines == b.plain_text_lines);
  REQUIRE(a.malformed_lines == b.malformed_lines);
  REQUIRE(a.oversized_lines == b.oversized_lines);
  REQUIRE(a.truncated_lines == b.truncated_lines);
  REQUIRE(a.has_truncated_final_line == b.has_truncated_final_line);
  REQUIRE(a.cancelled == b.cancelled);
  REQUIRE(a.limit_reached == b.limit_reached);
}

std::string build_hostile_corpus() {
  std::string data = "\xEF\xBB\xBF";
  for (int i = 1; i <= 160; ++i) {
    const std::string eol = (i % 3 == 0) ? "\r\n" : "\n";
    if (i % 24 == 0) {
      data += eol;
    } else if (i % 19 == 0) {
      data += R"({"broken":[1,2)" + eol;
    } else if (i % 17 == 0) {
      data += "plain text noise request_id=req-7 seq=" + std::to_string(i) + eol;
    } else if (i % 13 == 0) {
      data += std::string(140, 'M') + eol;
    } else if (i % 9 == 0) {
      data += R"({"timestamp":"t","level":"error","request_id":"req-\"7\"","msg":"esc"})" + eol;
    } else {
      const std::string rid = (i % 5 == 0) ? "req-7" : ("req-" + std::to_string(i % 4));
      const std::string lvl = (i % 2 == 0) ? "ERROR" : "INFO";
      data += R"({"timestamp":"t","level":")" + lvl + R"(","request_id":")" + rid +
              R"(","msg":"timeout seq )" + std::to_string(i) + "\"}" + eol;
    }
  }
  data += R"({"timestamp":"t","level":"ERROR","request_id":"req-7","msg":"eof)";
  return data;
}

} // namespace

TEST_CASE("MmapFile handles empty, BOM-only, and regular files safely", "[mmap]") {
  std::error_code ec;

  for (const std::string_view empty_data : {std::string_view(""), std::string_view("\xEF\xBB\xBF")}) {
    TempFile tmp(empty_data);
    auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
    REQUIRE(file.has_value());

    auto mapped = logloom::MmapFile::map_read_only(*file, ec);
    REQUIRE(mapped.has_value());
    REQUIRE_FALSE(ec);
    REQUIRE(mapped->is_mapped());
    REQUIRE(mapped->size() == empty_data.size());

    const auto stats = logloom::search_file_mmap(*mapped, {}, {}, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(stats.lines_scanned == 0);
    REQUIRE(stats.matches_found == 0);
  }
}

TEST_CASE("search_file_mmap matches pread search_file byte-for-byte across threads and partitions", "[mmap][differential]") {
  const std::string corpus = build_hostile_corpus();
  TempFile tmp(corpus);
  std::error_code ec;
  auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
  REQUIRE(file.has_value());
  auto mapped = logloom::MmapFile::map_read_only(*file, ec);
  REQUIRE(mapped.has_value());

  const std::vector<logloom::SearchQuery> queries = {
      logloom::SearchQuery{.reader_options = {.max_line_bytes = 128}},
      logloom::SearchQuery{.trace_id = "req-7", .reader_options = {.max_line_bytes = 128}},
      logloom::SearchQuery{.trace_id = std::string("req-\"7\""), .reader_options = {.max_line_bytes = 128}},
      logloom::SearchQuery{
          .level = "error",
          .text_query = "TIMEOUT",
          .case_sensitive = false,
          .reader_options = {.max_line_bytes = 128},
      },
      logloom::SearchQuery{
          .text_query = "timeout|esc",
          .case_sensitive = false,
          .is_regex = true,
          .reader_options = {.max_line_bytes = 128},
      },
  };

  for (const auto& q : queries) {
    std::vector<OwnedMmapMatch> expected_matches;
    const auto expected_stats = logloom::search_file(
        *file,
        q,
        [&](const logloom::SearchMatch& m) {
          expected_matches.push_back(capture_match(m));
          return true;
        },
        ec);
    REQUIRE_FALSE(ec);

    for (const std::size_t threads : {1U, 2U, 4U}) {
      for (const std::size_t part_bytes : {23U, 64U, 512U, 4096U}) {
        CAPTURE(threads, part_bytes);
        std::vector<OwnedMmapMatch> actual_matches;
        const auto actual_stats = logloom::search_file_mmap(
            *mapped,
            q,
            [&](const logloom::SearchMatch& m) {
              actual_matches.push_back(capture_match(m));
              return true;
            },
            ec,
            {.num_threads = threads, .partition_bytes = part_bytes});
        REQUIRE_FALSE(ec);
        REQUIRE(actual_matches == expected_matches);
        require_same_stats(actual_stats, expected_stats);
      }
    }
  }
}

TEST_CASE("search_file_mmap supports limit, cancellation, and invalid regex", "[mmap]") {
  const std::string corpus = build_hostile_corpus();
  TempFile tmp(corpus);
  std::error_code ec;
  auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
  REQUIRE(file.has_value());
  auto mapped = logloom::MmapFile::map_read_only(*file, ec);
  REQUIRE(mapped.has_value());

  std::vector<OwnedMmapMatch> seq_lim;
  const logloom::SearchQuery lim_q{.level = "ERROR", .limit = 4, .reader_options = {.max_line_bytes = 128}};
  const auto seq_stats = logloom::search_file(
      *file,
      lim_q,
      [&](const logloom::SearchMatch& m) {
        seq_lim.push_back(capture_match(m));
        return true;
      },
      ec);
  REQUIRE_FALSE(ec);
  REQUIRE(seq_stats.limit_reached);

  std::vector<OwnedMmapMatch> mmap_lim;
  const auto mmap_stats = logloom::search_file_mmap(
      *mapped,
      lim_q,
      [&](const logloom::SearchMatch& m) {
        mmap_lim.push_back(capture_match(m));
        return true;
      },
      ec,
      {.num_threads = 4, .partition_bytes = 64});
  REQUIRE_FALSE(ec);
  REQUIRE(mmap_stats.limit_reached);
  REQUIRE(mmap_lim == seq_lim);

  std::atomic<bool> cancel{true};
  const auto pre_cancel = logloom::search_file_mmap(
      *mapped, {}, {}, ec, {.num_threads = 4, .partition_bytes = 64}, &cancel);
  REQUIRE_FALSE(ec);
  REQUIRE(pre_cancel.cancelled);
  REQUIRE(pre_cancel.matches_found == 0);

  const auto bad_rx = logloom::search_file_mmap(
      *mapped, {.text_query = "[bad", .is_regex = true}, {}, ec);
  REQUIRE(ec == std::errc::invalid_argument);
  REQUIRE(bad_rx.matches_found == 0);
}
