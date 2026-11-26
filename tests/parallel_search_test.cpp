#include "logloom/search_engine.hpp"

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
            ("logloom_parallel_test_" + std::to_string(id) + ".jsonl");

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

struct OwnedSearchMatch {
  std::uint64_t line_number = 0;
  std::uint64_t byte_offset = 0;
  std::uint64_t byte_length = 0;
  std::string content{};
  std::optional<std::string> timestamp{};
  std::optional<std::string> level{};
  std::optional<std::string> trace_id{};
  bool has_crlf = false;
  bool is_truncated_eof = false;

  bool operator==(const OwnedSearchMatch&) const = default;
};

OwnedSearchMatch capture_match(const logloom::SearchMatch& m) {
  return OwnedSearchMatch{
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

std::string build_mixed_hostile_corpus() {
  std::string data = "\xEF\xBB\xBF";
  for (int i = 1; i <= 180; ++i) {
    const std::string eol = (i % 3 == 0) ? "\r\n" : "\n";
    if (i % 25 == 0) {
      data += eol; // empty line
    } else if (i % 19 == 0) {
      data += R"({"broken_json": [1, 2)" + eol; // malformed
    } else if (i % 17 == 0) {
      data += "plain text log entry request_id=req-7 seq=" + std::to_string(i) + eol;
    } else if (i % 13 == 0) {
      data += std::string(150, 'X') + eol; // oversized when max_line_bytes = 128
    } else if (i % 11 == 0) {
      data += R"({"timestamp":"2026-11-26T10:00:00Z","level":"WARN","nested":{"request_id":"req-7"},"msg":"nested ignore"})" + eol;
    } else if (i % 9 == 0) {
      data += R"({"timestamp":"2026-11-26T10:00:01Z","level":"error","request_id":"req-\"7\"","msg":"escaped trace"})" + eol;
    } else {
      const std::string rid = (i % 5 == 0) ? "req-7" : ("req-" + std::to_string(i % 4));
      const std::string lvl = (i % 2 == 0) ? "ERROR" : "INFO";
      data += R"({"timestamp":"2026-11-26T10:00:02Z","level":")" + lvl +
              R"(","request_id":")" + rid +
              R"(","msg":"db timeout seq )" + std::to_string(i) + "\"}" + eol;
    }
  }
  data += R"({"timestamp":"2026-11-26T10:59:59Z","level":"ERROR","request_id":"req-7","msg":"truncated-eof)";
  return data;
}

} // namespace

TEST_CASE("search_file_parallel matches search_file byte-for-byte across threads, partitions, and chunks", "[parallel_search][differential]") {
  const std::string corpus = build_mixed_hostile_corpus();
  TempFile tmp(corpus);
  std::error_code ec;
  auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
  REQUIRE(file.has_value());

  const std::vector<logloom::SearchQuery> queries = {
      logloom::SearchQuery{
          .reader_options = {.chunk_size = 64, .max_line_bytes = 128},
      },
      logloom::SearchQuery{
          .trace_id = "req-7",
          .reader_options = {.chunk_size = 64, .max_line_bytes = 128},
      },
      logloom::SearchQuery{
          .trace_id = std::string("req-\"7\""),
          .reader_options = {.chunk_size = 64, .max_line_bytes = 128},
      },
      logloom::SearchQuery{
          .level = "error",
          .text_query = "TIMEOUT",
          .case_sensitive = false,
          .reader_options = {.chunk_size = 64, .max_line_bytes = 128},
      },
      logloom::SearchQuery{
          .text_query = "timeout|escaped",
          .case_sensitive = false,
          .is_regex = true,
          .reader_options = {.chunk_size = 64, .max_line_bytes = 128},
      },
  };

  for (const auto& base_query : queries) {
    std::vector<OwnedSearchMatch> expected_matches;
    const auto expected_stats = logloom::search_file(
        *file,
        base_query,
        [&](const logloom::SearchMatch& m) {
          expected_matches.push_back(capture_match(m));
          return true;
        },
        ec);
    REQUIRE_FALSE(ec);

    for (const std::size_t threads : {1U, 2U, 4U, 8U}) {
      for (const std::size_t part_bytes : {16U, 37U, 64U, 128U, 512U, 4096U}) {
        for (const std::size_t chunk_sz : {3U, 13U, 64U}) {
          CAPTURE(threads, part_bytes, chunk_sz);
          logloom::SearchQuery q = base_query;
          q.reader_options.chunk_size = chunk_sz;

          std::vector<OwnedSearchMatch> actual_matches;
          const auto actual_stats = logloom::search_file_parallel(
              *file,
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
}

TEST_CASE("search_file_parallel enforces limit, cancellation, and invalid regex cleanly", "[parallel_search]") {
  const std::string corpus = build_mixed_hostile_corpus();
  TempFile tmp(corpus);
  std::error_code ec;
  auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
  REQUIRE(file.has_value());

  SECTION("limit returns exact first N matches in deterministic file order") {
    logloom::SearchQuery q{
        .level = "ERROR",
        .limit = 5,
        .reader_options = {.chunk_size = 32, .max_line_bytes = 128},
    };
    std::vector<OwnedSearchMatch> seq;
    const auto seq_stats = logloom::search_file(
        *file,
        q,
        [&](const logloom::SearchMatch& m) {
          seq.push_back(capture_match(m));
          return true;
        },
        ec);
    REQUIRE_FALSE(ec);
    REQUIRE(seq_stats.limit_reached);
    REQUIRE(seq.size() == 5);

    std::vector<OwnedSearchMatch> par;
    const auto par_stats = logloom::search_file_parallel(
        *file,
        q,
        [&](const logloom::SearchMatch& m) {
          par.push_back(capture_match(m));
          return true;
        },
        ec,
        {.num_threads = 4, .partition_bytes = 64});
    REQUIRE_FALSE(ec);
    REQUIRE(par_stats.limit_reached);
    REQUIRE(par_stats.matches_found == 5);
    REQUIRE(par == seq);
  }

  SECTION("pre-cancelled, mid-stream atomic cancel, and callback false stop cleanly") {
    std::atomic<bool> cancel{true};
    const auto pre_stats = logloom::search_file_parallel(
        *file, {}, {}, ec, {.num_threads = 4, .partition_bytes = 64}, &cancel);
    REQUIRE_FALSE(ec);
    REQUIRE(pre_stats.cancelled);
    REQUIRE(pre_stats.matches_found == 0);

    cancel.store(false);
    std::vector<std::uint64_t> mid_seen;
    const auto mid_stats = logloom::search_file_parallel(
        *file,
        {},
        [&](const logloom::SearchMatch& m) {
          mid_seen.push_back(m.line_number);
          if (mid_seen.size() == 3) {
            cancel.store(true, std::memory_order_relaxed);
          }
          return true;
        },
        ec,
        {.num_threads = 4, .partition_bytes = 64},
        &cancel);
    REQUIRE_FALSE(ec);
    REQUIRE(mid_seen.size() == 3);
    REQUIRE(mid_stats.cancelled);

    std::vector<std::uint64_t> cb_seen;
    const auto cb_stats = logloom::search_file_parallel(
        *file,
        {},
        [&](const logloom::SearchMatch& m) {
          cb_seen.push_back(m.line_number);
          return cb_seen.size() < 4;
        },
        ec,
        {.num_threads = 4, .partition_bytes = 64});
    REQUIRE_FALSE(ec);
    REQUIRE(cb_seen.size() == 4);
    REQUIRE(cb_stats.cancelled);
  }

  SECTION("invalid regex returns invalid_argument without spawning workers") {
    const auto stats = logloom::search_file_parallel(
        *file,
        {.text_query = "[bad(", .is_regex = true},
        {},
        ec,
        {.num_threads = 4, .partition_bytes = 64});
    REQUIRE(ec == std::errc::invalid_argument);
    REQUIRE(stats.matches_found == 0);
  }

  SECTION("empty callback counts matches accurately without materializing OwnedMatch") {
    const logloom::SearchQuery q{
        .level = "ERROR",
        .reader_options = {.chunk_size = 32, .max_line_bytes = 128},
    };
    const auto seq_stats = logloom::search_file(*file, q, {}, ec);
    REQUIRE_FALSE(ec);

    const auto par_stats = logloom::search_file_parallel(
        *file, q, {}, ec, {.num_threads = 4, .partition_bytes = 64});
    REQUIRE_FALSE(ec);
    require_same_stats(par_stats, seq_stats);

    logloom::SearchQuery lim_q = q;
    lim_q.limit = 5;
    const auto par_lim = logloom::search_file_parallel(
        *file, lim_q, {}, ec, {.num_threads = 4, .partition_bytes = 64});
    REQUIRE_FALSE(ec);
    REQUIRE(par_lim.matches_found == 5);
    REQUIRE(par_lim.limit_reached);
  }
}

TEST_CASE("search_file_parallel handles empty, BOM-only, and single-huge-line files", "[parallel_search][edge]") {
  std::error_code ec;

  for (const std::string_view empty_data : {std::string_view(""), std::string_view("\xEF\xBB\xBF")}) {
    TempFile tmp(empty_data);
    auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
    REQUIRE(file.has_value());

    const auto stats = logloom::search_file_parallel(
        *file, {}, {}, ec, {.num_threads = 4, .partition_bytes = 16});
    REQUIRE_FALSE(ec);
    REQUIRE(stats.lines_scanned == 0);
    REQUIRE(stats.matches_found == 0);
  }

  const std::string single_long =
      R"({"request_id":"long-1","payload":")" + std::string(300, 'a') + "\"}\n";
  TempFile tmp(single_long);
  auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
  REQUIRE(file.has_value());

  std::vector<OwnedSearchMatch> hits;
  const auto stats = logloom::search_file_parallel(
      *file,
      {.trace_id = "long-1"},
      [&](const logloom::SearchMatch& m) {
        hits.push_back(capture_match(m));
        return true;
      },
      ec,
      {.num_threads = 4, .partition_bytes = 16});
  REQUIRE_FALSE(ec);
  REQUIRE(stats.lines_scanned == 1);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].content + "\n" == single_long);
}
