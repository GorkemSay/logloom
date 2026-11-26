#include <charconv>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace {

void print_usage(std::ostream& out) {
  out << "Usage: logloom-gen --out <path> --size-mb <N> [options]\n"
      << "\n"
      << "Deterministic seeded JSONL/log dataset generator.\n"
      << "\n"
      << "Options:\n"
      << "  --out <path>          Output file path (required)\n"
      << "  --size-mb <N>         Target file size in MiB (required, > 0)\n"
      << "  --seed <N>            PRNG seed (default: 42)\n"
      << "  --target-trace <id>   Trace ID to inject deterministically (default: req-target-001)\n"
      << "  --trace-hits <N>      Number of target trace hits to inject (default: 50)\n"
      << "  --truncated-eof       Omit trailing newline on the final line\n"
      << "  -h, --help            Show this help message\n";
}

bool parse_u64(std::string_view text, std::uint64_t& out) noexcept {
  if (text.empty()) {
    return false;
  }
  const char* begin = text.data();
  const char* end = begin + text.size();
  std::uint64_t val = 0;
  const auto [ptr, ec] = std::from_chars(begin, end, val);
  if (ec != std::errc{} || ptr != end) {
    return false;
  }
  out = val;
  return true;
}

bool consume_flag(
    int argc,
    char* argv[],
    int& i,
    std::string_view flag,
    std::string_view& val,
    bool& err) {
  const std::string_view arg{argv[i]};
  if (arg == flag) {
    if (i + 1 >= argc) {
      std::cerr << "logloom-gen: option '" << flag << "' requires a value\n";
      err = true;
      return true;
    }
    val = argv[++i];
    return true;
  }
  if (arg.size() > flag.size() + 1 && arg.starts_with(flag) && arg[flag.size()] == '=') {
    val = arg.substr(flag.size() + 1);
    return true;
  }
  return false;
}

} // namespace

int main(int argc, char* argv[]) {
  std::optional<std::filesystem::path> out_path;
  std::uint64_t size_mb = 0;
  std::uint64_t seed = 42;
  std::string target_trace = "req-target-001";
  std::uint64_t trace_hits = 50;
  bool truncated_eof = false;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    std::string_view val;
    bool err = false;

    if (arg == "-h" || arg == "--help") {
      print_usage(std::cout);
      return 0;
    }
    if (arg == "--truncated-eof") {
      truncated_eof = true;
    } else if (consume_flag(argc, argv, i, "--out", val, err)) {
      if (err || val.empty()) {
        return 2;
      }
      out_path = std::filesystem::path(val);
    } else if (consume_flag(argc, argv, i, "--size-mb", val, err)) {
      if (err || !parse_u64(val, size_mb) || size_mb == 0) {
        std::cerr << "logloom-gen: --size-mb must be a positive integer\n";
        return 2;
      }
    } else if (consume_flag(argc, argv, i, "--seed", val, err)) {
      if (err || !parse_u64(val, seed)) {
        std::cerr << "logloom-gen: invalid --seed\n";
        return 2;
      }
    } else if (consume_flag(argc, argv, i, "--target-trace", val, err)) {
      if (err || val.empty()) {
        std::cerr << "logloom-gen: --target-trace cannot be empty\n";
        return 2;
      }
      target_trace = std::string(val);
    } else if (consume_flag(argc, argv, i, "--trace-hits", val, err)) {
      if (err || !parse_u64(val, trace_hits)) {
        std::cerr << "logloom-gen: invalid --trace-hits\n";
        return 2;
      }
    } else {
      std::cerr << "logloom-gen: unknown argument '" << arg << "'\n";
      return 2;
    }
  }

  if (!out_path.has_value() || size_mb == 0) {
    std::cerr << "logloom-gen: --out and --size-mb are required\n";
    return 2;
  }

  if (out_path->has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(out_path->parent_path(), ec);
  }

  std::ofstream out(*out_path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    std::cerr << "logloom-gen: cannot open '" << out_path->string() << "' for writing\n";
    return 2;
  }

  constexpr std::size_t kFlushThreshold = 1024 * 1024;
  std::string buffer;
  buffer.reserve(kFlushThreshold + 4096);

  const std::uint64_t target_bytes = size_mb * 1024ULL * 1024ULL;
  const std::uint64_t hit_interval =
      (trace_hits > 0) ? std::max<std::uint64_t>(target_bytes / trace_hits, 1024ULL) : 0ULL;
  std::uint64_t next_hit_byte = (hit_interval > 0) ? (hit_interval / 2ULL) : 0ULL;
  std::uint64_t hits_emitted = 0;

  static constexpr const char* kLevels[] = {"INFO", "INFO", "INFO", "WARN", "ERROR", "DEBUG"};
  static constexpr const char* kServices[] = {
      "api-gateway", "auth-svc", "checkout-api", "billing-worker", "inventory-db"};
  static constexpr const char* kMessages[] = {
      "request completed successfully after cache lookup",
      "upstream connection pooled and reused within deadline",
      "retrying transient timeout on downstream shard",
      "database query exceeded soft latency threshold",
      "session token validated against local key ring",
      "payload schema verified and queued for settlement",
  };

  std::mt19937_64 rng(seed);
  std::uint64_t total_written = 0;
  std::uint64_t line_count = 0;
  char line_buf[512];

  while (total_written + buffer.size() < target_bytes) {
    const std::uint64_t approx_pos = total_written + buffer.size();
    const std::uint64_t roll = rng() % 1000ULL;
    const std::uint64_t sec = line_count % 60ULL;
    const std::uint64_t min = (line_count / 60ULL) % 60ULL;
    const std::uint64_t hr = (line_count / 3600ULL) % 24ULL;
    const std::uint64_t usec = (line_count * 137ULL) % 1000000ULL;

    int len = 0;
    if (hit_interval > 0 && hits_emitted < trace_hits && approx_pos >= next_hit_byte) {
      const char* lvl = (hits_emitted == trace_hits - 1) ? "ERROR" : "INFO";
      const char* svc = kServices[hits_emitted % 5];
      len = std::snprintf(
          line_buf,
          sizeof(line_buf),
          "{\"timestamp\":\"2026-11-26T%02llu:%02llu:%02llu.%06lluZ\",\"level\":\"%s\","
          "\"service\":\"%s\",\"request_id\":\"%s\",\"seq\":%llu,"
          "\"msg\":\"trace span checkpoint recorded across service boundary\"}\n",
          static_cast<unsigned long long>(hr),
          static_cast<unsigned long long>(min),
          static_cast<unsigned long long>(sec),
          static_cast<unsigned long long>(usec),
          lvl,
          svc,
          target_trace.c_str(),
          static_cast<unsigned long long>(hits_emitted));
      ++hits_emitted;
      next_hit_byte += hit_interval;
    } else if (roll < 10ULL) {
      len = std::snprintf(
          line_buf,
          sizeof(line_buf),
          "2026-11-26T%02llu:%02llu:%02llu.%06lluZ [worker-%llu] heartbeat ok queue_depth=%llu\n",
          static_cast<unsigned long long>(hr),
          static_cast<unsigned long long>(min),
          static_cast<unsigned long long>(sec),
          static_cast<unsigned long long>(usec),
          static_cast<unsigned long long>(rng() % 8ULL),
          static_cast<unsigned long long>(rng() % 256ULL));
    } else if (roll < 12ULL) {
      len = std::snprintf(
          line_buf,
          sizeof(line_buf),
          "{\"timestamp\":\"2026-11-26T%02llu:%02llu:%02llu.%06lluZ\",\"level\":\"ERROR\","
          "\"malformed\":[1,2\n",
          static_cast<unsigned long long>(hr),
          static_cast<unsigned long long>(min),
          static_cast<unsigned long long>(sec),
          static_cast<unsigned long long>(usec));
    } else {
      const char* lvl = kLevels[rng() % 6ULL];
      const char* svc = kServices[rng() % 5ULL];
      const char* msg = kMessages[rng() % 6ULL];
      const std::uint64_t rid = (rng() % 900000ULL) + 100000ULL;
      const std::uint64_t latency = (rng() % 450ULL) + 1ULL;
      len = std::snprintf(
          line_buf,
          sizeof(line_buf),
          "{\"timestamp\":\"2026-11-26T%02llu:%02llu:%02llu.%06lluZ\",\"level\":\"%s\","
          "\"service\":\"%s\",\"request_id\":\"req-%06llu\",\"latency_ms\":%llu,"
          "\"status\":200,\"msg\":\"%s\"}\n",
          static_cast<unsigned long long>(hr),
          static_cast<unsigned long long>(min),
          static_cast<unsigned long long>(sec),
          static_cast<unsigned long long>(usec),
          lvl,
          svc,
          static_cast<unsigned long long>(rid),
          static_cast<unsigned long long>(latency),
          msg);
    }

    if (len > 0) {
      buffer.append(line_buf, static_cast<std::size_t>(len));
      ++line_count;
    }

    if (buffer.size() >= kFlushThreshold) {
      out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      if (!out.good()) {
        std::cerr << "logloom-gen: write failure\n";
        return 2;
      }
      total_written += buffer.size();
      buffer.clear();
    }
  }

  if (truncated_eof && !buffer.empty() && buffer.back() == '\n') {
    buffer.pop_back();
  }

  if (!buffer.empty()) {
    out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    total_written += buffer.size();
  }
  out.flush();
  if (!out.good()) {
    std::cerr << "logloom-gen: flush failure\n";
    return 2;
  }

  std::cout << "generated " << out_path->string()
            << " bytes=" << total_written
            << " lines=" << line_count
            << " target_hits=" << hits_emitted << '\n';
  return 0;
}
