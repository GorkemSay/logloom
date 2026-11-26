#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace {

class TempFile {
public:
  explicit TempFile(std::string_view bytes, std::string_view suffix = ".jsonl") {
    static std::atomic<std::uint64_t> counter{0};
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    path_ = std::filesystem::temp_directory_path() /
            ("logloom_cli_test_" + std::to_string(id) + std::string(suffix));

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

struct CliResult {
  int exit_code = -1;
  std::string out;
  std::string err;
};

CliResult run_cli(std::vector<std::string> args) {
  std::array<int, 2> out_pipe{-1, -1};
  std::array<int, 2> err_pipe{-1, -1};
  REQUIRE(::pipe(out_pipe.data()) == 0);
  REQUIRE(::pipe(err_pipe.data()) == 0);

  const pid_t pid = ::fork();
  REQUIRE(pid >= 0);

  if (pid == 0) {
    ::dup2(out_pipe[1], STDOUT_FILENO);
    ::dup2(err_pipe[1], STDERR_FILENO);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);
    ::close(err_pipe[0]);
    ::close(err_pipe[1]);

    std::vector<std::string> argv_storage;
    argv_storage.reserve(args.size() + 1);
    argv_storage.emplace_back(LOGLOOM_CLI_PATH);
    for (auto& a : args) {
      argv_storage.push_back(std::move(a));
    }

    std::vector<char*> argv_ptrs;
    argv_ptrs.reserve(argv_storage.size() + 1);
    for (auto& s : argv_storage) {
      argv_ptrs.push_back(s.data());
    }
    argv_ptrs.push_back(nullptr);

    ::execv(LOGLOOM_CLI_PATH, argv_ptrs.data());
    ::_exit(127);
  }

  ::close(out_pipe[1]);
  ::close(err_pipe[1]);

  CliResult result;
  std::array<pollfd, 2> fds{{
      {.fd = out_pipe[0], .events = POLLIN, .revents = 0},
      {.fd = err_pipe[0], .events = POLLIN, .revents = 0},
  }};
  std::array<char, 4096> buf{};

  while (fds[0].fd >= 0 || fds[1].fd >= 0) {
    const int rc = ::poll(fds.data(), fds.size(), -1);
    if (rc < 0 && errno == EINTR) {
      continue;
    }
    REQUIRE(rc >= 0);

    for (std::size_t i = 0; i < fds.size(); ++i) {
      if (fds[i].fd < 0) {
        continue;
      }
      if ((fds[i].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        const ssize_t n = ::read(fds[i].fd, buf.data(), buf.size());
        if (n > 0) {
          if (i == 0) {
            result.out.append(buf.data(), static_cast<std::size_t>(n));
          } else {
            result.err.append(buf.data(), static_cast<std::size_t>(n));
          }
        } else if (n == 0 || errno != EINTR) {
          ::close(fds[i].fd);
          fds[i].fd = -1;
        }
      }
    }
  }

  int status = 0;
  pid_t waited = -1;
  do {
    waited = ::waitpid(pid, &status, 0);
  } while (waited < 0 && errno == EINTR);
  REQUIRE(waited == pid);

  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

std::string read_file_text(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  REQUIRE(in.is_open());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

} // namespace

TEST_CASE("logloom CLI handles global flags and error paths", "[cli]") {
  const auto ver = run_cli({"--version"});
  REQUIRE(ver.exit_code == 0);
  REQUIRE(ver.out == "logloom 1.0.0\n");
  REQUIRE(ver.err.empty());

  const auto help = run_cli({"--help"});
  REQUIRE(help.exit_code == 0);
  REQUIRE(help.out.find("Usage: logloom") != std::string::npos);

  const auto bad_cmd = run_cli({"bogus"});
  REQUIRE(bad_cmd.exit_code == 2);
  REQUIRE(bad_cmd.out.empty());
  REQUIRE(bad_cmd.err.find("unknown command") != std::string::npos);

  const auto missing = run_cli({"stats", "/tmp/logloom_nonexistent_file_xyz.jsonl"});
  REQUIRE(missing.exit_code == 2);
  REQUIRE(missing.out.empty());
  REQUIRE_FALSE(missing.err.empty());
}

TEST_CASE("logloom trace isolates traces, merges context hunks, and exports safely", "[cli][trace]") {
  const std::string log_data =
      R"({"timestamp":"t1","level":"INFO","request_id":"req-1","msg":"start"})" "\n"
      "plain noise 2\n"
      "plain noise 3\n"
      "plain noise 4\n"
      "plain noise 5\n"
      R"({"timestamp":"t6","level":"WARN","request_id":"req-1","trace":"custom-9","msg":"retry"})" "\n"
      R"({"timestamp":"t7","level":"ERROR","request_id":"req-2","msg":"other"})" "\n"
      R"({"timestamp":"t8","level":"ERROR","request_id":"req-1","msg":"boom"})" "\n"
      R"({"timestamp":"t9","level":"INFO","request_id":"req-3","msg":"partial)";

  TempFile log_file(log_data);

  SECTION("basic trace output has no ANSI escapes when piped") {
    const auto res = run_cli({"trace", log_file.path().string(), "--id", "req-1"});
    REQUIRE(res.exit_code == 0);
    REQUIRE(res.out.find('\x1b') == std::string::npos);
    REQUIRE(res.out.find("1:{\"timestamp\":\"t1\"") != std::string::npos);
    REQUIRE(res.out.find("6:{\"timestamp\":\"t6\"") != std::string::npos);
    REQUIRE(res.out.find("8:{\"timestamp\":\"t8\"") != std::string::npos);
    REQUIRE(res.out.find("req-2") == std::string::npos);
    // Truncated EOF warning goes to stderr only.
    REQUIRE(res.err.find("truncated final line") != std::string::npos);
  }

  SECTION("trace --context merges overlapping intervals and separates disjoint hunks with --") {
    const auto res = run_cli(
        {"trace", log_file.path().string(), "--id", "req-1", "--context", "1"});
    REQUIRE(res.exit_code == 0);
    const std::string expected =
        "1:{\"timestamp\":\"t1\",\"level\":\"INFO\",\"request_id\":\"req-1\",\"msg\":\"start\"}\n"
        "2-plain noise 2\n"
        "--\n"
        "5-plain noise 5\n"
        "6:{\"timestamp\":\"t6\",\"level\":\"WARN\",\"request_id\":\"req-1\",\"trace\":\"custom-9\",\"msg\":\"retry\"}\n"
        "7-{\"timestamp\":\"t7\",\"level\":\"ERROR\",\"request_id\":\"req-2\",\"msg\":\"other\"}\n"
        "8:{\"timestamp\":\"t8\",\"level\":\"ERROR\",\"request_id\":\"req-1\",\"msg\":\"boom\"}\n"
        "9-{\"timestamp\":\"t9\",\"level\":\"INFO\",\"request_id\":\"req-3\",\"msg\":\"partial\n";
    REQUIRE(res.out == expected);
  }

  SECTION("trace --export writes raw JSONL without modifying input and supports custom --key") {
    TempFile out_file("", ".jsonl");
    const auto res = run_cli({
        "trace",
        log_file.path().string(),
        "--id",
        "req-1",
        "--export",
        out_file.path().string(),
    });
    REQUIRE(res.exit_code == 0);
    REQUIRE(read_file_text(log_file.path()) == log_data);
    REQUIRE(read_file_text(out_file.path()) ==
            R"({"timestamp":"t1","level":"INFO","request_id":"req-1","msg":"start"})" "\n"
            R"({"timestamp":"t6","level":"WARN","request_id":"req-1","trace":"custom-9","msg":"retry"})" "\n"
            R"({"timestamp":"t8","level":"ERROR","request_id":"req-1","msg":"boom"})" "\n");

    const auto custom = run_cli({
        "trace",
        log_file.path().string(),
        "--id",
        "custom-9",
        "--key",
        "trace",
    });
    REQUIRE(custom.exit_code == 0);
    REQUIRE(custom.out ==
            "6:{\"timestamp\":\"t6\",\"level\":\"WARN\",\"request_id\":\"req-1\",\"trace\":\"custom-9\",\"msg\":\"retry\"}\n");
  }

  SECTION("refuses --export targeting input file and returns 1 on no match") {
    const auto same = run_cli({
        "trace",
        log_file.path().string(),
        "--id",
        "req-1",
        "--export",
        log_file.path().string(),
    });
    REQUIRE(same.exit_code == 2);
    REQUIRE(same.err.find("refusing to overwrite") != std::string::npos);
    REQUIRE(read_file_text(log_file.path()) == log_data);

    const auto nomatch = run_cli({"trace", log_file.path().string(), "--id", "req-999"});
    REQUIRE(nomatch.exit_code == 1);
    REQUIRE(nomatch.out.empty());

    const auto untouched_export =
        std::filesystem::temp_directory_path() / "logloom_should_not_be_created.jsonl";
    std::error_code rm_ec;
    std::filesystem::remove(untouched_export, rm_ec);
    const auto missing_in = run_cli({
        "trace",
        "/tmp/logloom_missing_input_98765.jsonl",
        "--id",
        "req-1",
        "--export",
        untouched_export.string(),
    });
    REQUIRE(missing_in.exit_code == 2);
    REQUIRE_FALSE(std::filesystem::exists(untouched_export));
  }
}

TEST_CASE("logloom search, context, and stats subcommands work end-to-end", "[cli]") {
  const std::string log_data =
      R"({"level":"INFO","msg":"boot"})" "\n"
      "plain error line\n"
      R"({"level":"error","msg":"Timeout hit"})" "\n"
      "{malformed\n"
      R"({"level":"ERROR","msg":"second timeout"})" "\n";

  TempFile log_file(log_data);

  SECTION("search with --query, -i, --regex, --level, --limit, and error paths") {
    const auto q_res = run_cli({
        "search",
        log_file.path().string(),
        "--query",
        "timeout",
        "-i",
        "--level",
        "ERROR",
        "--limit",
        "1",
    });
    REQUIRE(q_res.exit_code == 0);
    REQUIRE(q_res.out == "3:{\"level\":\"error\",\"msg\":\"Timeout hit\"}\n");

    const auto rx_res = run_cli({
        "search",
        log_file.path().string(),
        "--query",
        "boot|second",
        "--regex",
    });
    REQUIRE(rx_res.exit_code == 0);
    REQUIRE(rx_res.out ==
            "1:{\"level\":\"INFO\",\"msg\":\"boot\"}\n"
            "5:{\"level\":\"ERROR\",\"msg\":\"second timeout\"}\n");

    const auto bad_rx = run_cli({
        "search",
        log_file.path().string(),
        "--query",
        "[unclosed",
        "--regex",
    });
    REQUIRE(bad_rx.exit_code == 2);
    REQUIRE(bad_rx.err.find("invalid regular expression") != std::string::npos);

    const auto none = run_cli({"search", log_file.path().string(), "--query", "never_seen"});
    REQUIRE(none.exit_code == 1);
    REQUIRE(none.out.empty());
  }

  SECTION("context prints target with colon, surroundings with dash, and handles out-of-range") {
    const auto ctx = run_cli({
        "context",
        log_file.path().string(),
        "--line",
        "3",
        "--before",
        "1",
        "--after",
        "1",
    });
    REQUIRE(ctx.exit_code == 0);
    REQUIRE(ctx.out ==
            "2-plain error line\n"
            "3:{\"level\":\"error\",\"msg\":\"Timeout hit\"}\n"
            "4-{malformed\n");

    const auto oor = run_cli({"context", log_file.path().string(), "--line", "99"});
    REQUIRE(oor.exit_code == 1);
    REQUIRE(oor.out.empty());
    REQUIRE(oor.err.find("out of range") != std::string::npos);
  }

  SECTION("stats outputs complete line classification and size") {
    const auto st = run_cli({"stats", log_file.path().string()});
    REQUIRE(st.exit_code == 0);
    REQUIRE(st.err.empty());
    REQUIRE(st.out.find("size_bytes: " + std::to_string(log_data.size()) + "\n") != std::string::npos);
    REQUIRE(st.out.find("lines: 5\n") != std::string::npos);
    REQUIRE(st.out.find("json_lines: 3\n") != std::string::npos);
    REQUIRE(st.out.find("plain_text_lines: 1\n") != std::string::npos);
    REQUIRE(st.out.find("malformed_lines: 1\n") != std::string::npos);
    REQUIRE(st.out.find("oversized_lines: 0\n") != std::string::npos);
    REQUIRE(st.out.find("truncated_eof: no\n") != std::string::npos);
  }
}
