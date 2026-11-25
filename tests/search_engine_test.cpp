#include "logloom/search_engine.hpp"

#include <catch2/catch_test_macros.hpp>

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
            ("logloom_search_test_" + std::to_string(id) + ".jsonl");

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

struct CapturedMatch {
  std::uint64_t line_number = 0;
  std::uint64_t byte_offset = 0;
  std::string content{};
  std::optional<std::string> timestamp{};
  std::optional<std::string> level{};
  std::optional<std::string> trace_id{};
};

std::vector<CapturedMatch> run_search(
    const logloom::PosixFile& file,
    const logloom::SearchQuery& query,
    logloom::SearchStats* out_stats = nullptr,
    const std::atomic<bool>* cancel_flag = nullptr) {
  std::vector<CapturedMatch> matches;
  std::error_code ec;
  const auto stats = logloom::search_file(
      file,
      query,
      [&](const logloom::SearchMatch& m) {
        matches.push_back(CapturedMatch{
            .line_number = m.line_number,
            .byte_offset = m.byte_offset,
            .content = std::string(m.content),
            .timestamp = m.fields.timestamp ? std::optional<std::string>(*m.fields.timestamp)
                                            : std::nullopt,
            .level = m.fields.level ? std::optional<std::string>(*m.fields.level) : std::nullopt,
            .trace_id = m.fields.trace_id ? std::optional<std::string>(*m.fields.trace_id)
                                          : std::nullopt,
        });
        return true;
      },
      ec,
      cancel_flag);
  REQUIRE_FALSE(ec);
  if (out_stats != nullptr) {
    *out_stats = stats;
  }
  return matches;
}

} // namespace

TEST_CASE("search_file isolates exact trace_id and respects custom trace_key and escapes", "[search]") {
  const std::string data =
      R"({"timestamp":"t1","level":"INFO","request_id":"req-1","msg":"start"})" "\n"
      R"({"timestamp":"t2","level":"INFO","nested":{"request_id":"req-1"},"msg":"nested must ignore"})" "\n"
      R"({"timestamp":"t3","level":"ERROR","request_id":"req-2","msg":"other"})" "\n"
      R"({"timestamp":"t4","level":"WARN","request_id":"req-1","msg":"retry"})" "\n"
      R"({"timestamp":"t5","level":"INFO","trace":"tr-\"quoted\"\\1","msg":"escaped"})" "\n";

  TempFile tmp(data);
  std::error_code ec;
  auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
  REQUIRE(file.has_value());

  logloom::SearchStats stats{};
  const auto hits = run_search(*file, {.trace_id = "req-1"}, &stats);
  REQUIRE(hits.size() == 2);
  REQUIRE(hits[0].line_number == 1);
  REQUIRE(hits[0].timestamp == "t1");
  REQUIRE(hits[1].line_number == 4);
  REQUIRE(hits[1].level == "WARN");
  REQUIRE(stats.lines_scanned == 5);
  REQUIRE(stats.matches_found == 2);
  REQUIRE(stats.json_lines == 5);

  const auto custom_hits = run_search(*file, {
      .trace_id = std::string("tr-\"quoted\"\\1"),
      .trace_key = "trace",
  });
  REQUIRE(custom_hits.size() == 1);
  REQUIRE(custom_hits[0].line_number == 5);
}

TEST_CASE("search_file supports level, substring, regex, and combined filters", "[search]") {
  const std::string data =
      "{\"timestamp\":\"t1\",\"level\":\"error\",\"request_id\":\"r1\",\"msg\":\"DB Timeout\"}\n"
      "{\"timestamp\":\"t2\",\"level\":\"ERROR\",\"request_id\":\"r2\",\"msg\":\"auth failed\"}\n"
      "{\"timestamp\":\"t3\",\"level\":\"INFO\",\"request_id\":\"r1\",\"msg\":\"db timeout recovered\"}\n"
      "plain text line with db timeout\n";

  TempFile tmp(data);
  std::error_code ec;
  auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
  REQUIRE(file.has_value());

  SECTION("case-insensitive level filter") {
    const auto hits = run_search(*file, {.level = "ERROR"});
    REQUIRE(hits.size() == 2);
    REQUIRE(hits[0].line_number == 1);
    REQUIRE(hits[1].line_number == 2);
  }

  SECTION("case-sensitive vs case-insensitive substring search") {
    const auto cs = run_search(*file, {.text_query = "DB Timeout", .case_sensitive = true});
    REQUIRE(cs.size() == 1);
    REQUIRE(cs[0].line_number == 1);

    const auto ci = run_search(*file, {.text_query = "DB Timeout", .case_sensitive = false});
    REQUIRE(ci.size() == 3);
    REQUIRE(ci[0].line_number == 1);
    REQUIRE(ci[1].line_number == 3);
    REQUIRE(ci[2].line_number == 4);
  }

  SECTION("regex search and combined AND filters") {
    const auto rx = run_search(*file, {
        .level = "error",
        .text_query = "timeout|failed",
        .case_sensitive = false,
        .is_regex = true,
    });
    REQUIRE(rx.size() == 2);
    REQUIRE(rx[0].line_number == 1);
    REQUIRE(rx[1].line_number == 2);

    const auto combined = run_search(*file, {
        .trace_id = "r1",
        .level = "ERROR",
        .text_query = "Timeout",
    });
    REQUIRE(combined.size() == 1);
    REQUIRE(combined[0].line_number == 1);
  }

  SECTION("invalid regex returns invalid_argument without throwing") {
    logloom::SearchQuery bad{.text_query = "[unclosed(", .is_regex = true};
    const auto stats = logloom::search_file(*file, bad, {}, ec);
    REQUIRE(ec == std::errc::invalid_argument);
    REQUIRE(stats.lines_scanned == 0);
    REQUIRE(stats.matches_found == 0);
  }
}

TEST_CASE("search_file supports limit, cancellation, and SearchStats on mixed/hostile input", "[search][stats]") {
  const std::string data =
      "{\"level\":\"INFO\",\"request_id\":\"a\"}\n"
      "plain text hit\n"
      "{\"broken\":\n"
      + std::string(40, 'Z') + "\n" // oversized when max_line_bytes = 20
      "{\"level\":\"ERROR\",\"request_id\":\"a\"}\n"
      "{\"level\":\"INFO\",\"partial\":true"; // truncated EOF

  TempFile tmp(data);
  std::error_code ec;
  auto file = logloom::PosixFile::open_read_only(tmp.path(), ec);
  REQUIRE(file.has_value());

  SECTION("full scan reports accurate SearchStats across all line categories") {
    logloom::SearchStats stats{};
    const auto hits = run_search(*file, {
        .trace_id = "a",
        .reader_options = {.chunk_size = 16, .max_line_bytes = 38},
    }, &stats);

    REQUIRE(hits.size() == 2);
    REQUIRE(hits[0].line_number == 1);
    REQUIRE(hits[1].line_number == 5);

    REQUIRE(stats.lines_scanned == 6);
    REQUIRE(stats.matches_found == 2);
    REQUIRE(stats.json_lines == 2);
    REQUIRE(stats.plain_text_lines == 1);
    REQUIRE(stats.malformed_lines == 2);
    REQUIRE(stats.oversized_lines == 1);
    REQUIRE(stats.truncated_lines == 1);
    REQUIRE(stats.has_truncated_final_line);
    REQUIRE_FALSE(stats.cancelled);
    REQUIRE_FALSE(stats.limit_reached);
  }

  SECTION("limit stops scan early and sets limit_reached") {
    logloom::SearchStats stats{};
    const auto hits = run_search(*file, {.limit = 2}, &stats);
    REQUIRE(hits.size() == 2);
    REQUIRE(stats.matches_found == 2);
    REQUIRE(stats.lines_scanned == 2);
    REQUIRE(stats.limit_reached);
    REQUIRE_FALSE(stats.cancelled);
  }

  SECTION("pre-cancelled and mid-stream atomic cancel_flag stops immediately") {
    std::atomic<bool> cancel{true};
    logloom::SearchStats pre_stats{};
    const auto pre_hits = run_search(*file, {}, &pre_stats, &cancel);
    REQUIRE(pre_hits.empty());
    REQUIRE(pre_stats.lines_scanned == 0);
    REQUIRE(pre_stats.cancelled);

    cancel.store(false);
    std::vector<std::uint64_t> seen;
    const auto mid_stats = logloom::search_file(
        *file,
        {},
        [&](const logloom::SearchMatch& m) {
          seen.push_back(m.line_number);
          cancel.store(true, std::memory_order_relaxed);
          return true;
        },
        ec,
        &cancel);
    REQUIRE_FALSE(ec);
    REQUIRE(seen == std::vector<std::uint64_t>{1});
    REQUIRE(mid_stats.lines_scanned == 1);
    REQUIRE(mid_stats.cancelled);
  }

  SECTION("callback returning false cancels cleanly") {
    std::vector<std::uint64_t> seen;
    const auto stats = logloom::search_file(
        *file,
        {},
        [&](const logloom::SearchMatch& m) {
          seen.push_back(m.line_number);
          return seen.size() < 2;
        },
        ec);
    REQUIRE_FALSE(ec);
    REQUIRE(seen == std::vector<std::uint64_t>{1, 2});
    REQUIRE(stats.matches_found == 2);
    REQUIRE(stats.cancelled);
  }
}
