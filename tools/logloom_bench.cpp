#include "logloom/line_index.hpp"
#include "logloom/mmap_reader.hpp"
#include "logloom/posix_file.hpp"
#include "logloom/search_engine.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <system_error>

namespace {

void print_usage(std::ostream& out) {
  out << "Usage: logloom-bench --file <path> --mode <mode> [options]\n"
      << "\n"
      << "Modes:\n"
      << "  index           Build adaptive LineIndex\n"
      << "  pread-single    Single-threaded chunked pread search\n"
      << "  pread-parallel  Multithreaded chunked pread search\n"
      << "  mmap-single     Single-threaded mmap search\n"
      << "  mmap-parallel   Multithreaded mmap search\n"
      << "\n"
      << "Options:\n"
      << "  --trace-id <id>   Exact request_id filter\n"
      << "  --query <text>    Substring query filter\n"
      << "  --threads <N>     Worker thread count for parallel modes (default: auto)\n"
      << "  -h, --help        Show this help message\n";
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
      std::cerr << "logloom-bench: option '" << flag << "' requires a value\n";
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

double peak_rss_mib() noexcept {
  struct rusage ru {};
  if (::getrusage(RUSAGE_SELF, &ru) != 0) {
    return 0.0;
  }
#ifdef __APPLE__
  return static_cast<double>(ru.ru_maxrss) / (1024.0 * 1024.0);
#else
  return static_cast<double>(ru.ru_maxrss) / 1024.0;
#endif
}

} // namespace

int main(int argc, char* argv[]) {
  std::optional<std::filesystem::path> file_path;
  std::string mode;
  std::optional<std::string> trace_id;
  std::optional<std::string> text_query;
  std::size_t threads = 0;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    std::string_view val;
    bool err = false;

    if (arg == "-h" || arg == "--help") {
      print_usage(std::cout);
      return 0;
    }
    if (consume_flag(argc, argv, i, "--file", val, err)) {
      if (err || val.empty()) {
        return 2;
      }
      file_path = std::filesystem::path(val);
    } else if (consume_flag(argc, argv, i, "--mode", val, err)) {
      if (err || val.empty()) {
        return 2;
      }
      mode = std::string(val);
    } else if (consume_flag(argc, argv, i, "--trace-id", val, err)) {
      if (err) {
        return 2;
      }
      trace_id = std::string(val);
    } else if (consume_flag(argc, argv, i, "--query", val, err)) {
      if (err) {
        return 2;
      }
      text_query = std::string(val);
    } else if (consume_flag(argc, argv, i, "--threads", val, err)) {
      std::uint64_t parsed = 0;
      if (err || !parse_u64(val, parsed) || parsed == 0) {
        std::cerr << "logloom-bench: --threads must be a positive integer\n";
        return 2;
      }
      threads = static_cast<std::size_t>(parsed);
    } else {
      std::cerr << "logloom-bench: unknown argument '" << arg << "'\n";
      return 2;
    }
  }

  if (!file_path.has_value() || mode.empty()) {
    std::cerr << "logloom-bench: --file and --mode are required\n";
    return 2;
  }

  std::error_code ec;
  const auto file = logloom::PosixFile::open_read_only(*file_path, ec);
  if (!file.has_value()) {
    std::cerr << "logloom-bench: open failed: " << ec.message() << '\n';
    return 2;
  }

  const std::uint64_t file_bytes = file->size();
  logloom::SearchQuery query;
  query.trace_id = trace_id;
  query.text_query = text_query;

  using Clock = std::chrono::steady_clock;
  const auto t0 = Clock::now();
  bool has_ttfm = false;
  double ttfm_ms = 0.0;
  std::uint64_t lines = 0;
  std::uint64_t matches = 0;
  double index_mem_mib = 0.0;
  std::uint64_t index_stride = 0;

  auto on_match = [&](const logloom::SearchMatch&) {
    if (!has_ttfm) {
      ttfm_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
      has_ttfm = true;
    }
    return true;
  };

  if (mode == "index") {
    const auto idx = logloom::LineIndex::build(*file, ec);
    if (!idx.has_value()) {
      std::cerr << "logloom-bench: index failed: " << ec.message() << '\n';
      return 2;
    }
    lines = idx->line_count();
    index_mem_mib = static_cast<double>(idx->memory_bytes()) / (1024.0 * 1024.0);
    index_stride = idx->stride();
  } else if (mode == "pread-single") {
    const auto stats = logloom::search_file(*file, query, on_match, ec);
    if (ec) {
      std::cerr << "logloom-bench: pread-single failed: " << ec.message() << '\n';
      return 2;
    }
    lines = stats.lines_scanned;
    matches = stats.matches_found;
  } else if (mode == "pread-parallel") {
    const auto stats = logloom::search_file_parallel(
        *file, query, on_match, ec, {.num_threads = threads});
    if (ec) {
      std::cerr << "logloom-bench: pread-parallel failed: " << ec.message() << '\n';
      return 2;
    }
    lines = stats.lines_scanned;
    matches = stats.matches_found;
  } else if (mode == "mmap-single" || mode == "mmap-parallel") {
    const auto mapped = logloom::MmapFile::map_read_only(*file, ec);
    if (!mapped.has_value()) {
      std::cerr << "logloom-bench: mmap failed: " << ec.message() << '\n';
      return 2;
    }
    const std::size_t n_threads = (mode == "mmap-single") ? 1U : threads;
    const auto stats = logloom::search_file_mmap(
        *mapped, query, on_match, ec, {.num_threads = n_threads});
    if (ec) {
      std::cerr << "logloom-bench: " << mode << " failed: " << ec.message() << '\n';
      return 2;
    }
    lines = stats.lines_scanned;
    matches = stats.matches_found;
  } else {
    std::cerr << "logloom-bench: unknown mode '" << mode << "'\n";
    return 2;
  }

  const double duration_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
  const double duration_s = duration_ms > 0.0 ? (duration_ms / 1000.0) : 1e-9;
  const double throughput_mb_s =
      (static_cast<double>(file_bytes) / (1024.0 * 1024.0)) / duration_s;

  std::cout << std::fixed << std::setprecision(2)
            << "mode=" << mode
            << " file_bytes=" << file_bytes
            << " duration_ms=" << duration_ms
            << " ttfm_ms=";
  if (has_ttfm) {
    std::cout << ttfm_ms;
  } else {
    std::cout << "N/A";
  }
  std::cout << " throughput_mib_s=" << throughput_mb_s
            << " lines=" << lines
            << " matches=" << matches
            << " peak_rss_mib=" << peak_rss_mib();
  if (mode == "index") {
    std::cout << " index_mem_mib=" << index_mem_mib
              << " stride=" << index_stride;
  }
  std::cout << '\n';
  return 0;
}
