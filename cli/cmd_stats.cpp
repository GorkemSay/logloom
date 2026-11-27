#include "arg_parser.hpp"
#include "cli_common.hpp"
#include "commands.hpp"

#include "logloom/search_engine.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <system_error>

namespace logloom::cli {
namespace {

struct StatsOptions {
  std::optional<std::filesystem::path> file_path;
  std::size_t threads = 0;
};

void print_stats_usage(std::ostream& out) {
  out << "Usage: logloom stats <file>\n"
      << "\n"
      << "Options:\n"
      << "  --threads <N>      Worker thread count (default: auto)\n"
      << "  -h, --help         Show this help message\n";
}

ParseStatus parse_stats_options(int argc, char* argv[], StatsOptions& opts) {
  ArgParser parser("stats", print_stats_usage);
  parser.add_positional_file(opts.file_path);

  parser.add_u64_option(
      "--threads",
      true,
      [&](std::uint64_t parsed) { opts.threads = static_cast<std::size_t>(parsed); },
      "logloom: --threads must be a positive integer\n");

  const ParseStatus status = parser.parse(argc, argv);
  if (status != ParseStatus::Ok) {
    return status;
  }
  if (!opts.file_path.has_value()) {
    std::cerr << "logloom stats: <file> is required\n";
    return ParseStatus::Error;
  }
  return ParseStatus::Ok;
}

} // namespace

int run_stats(int argc, char* argv[]) {
  StatsOptions opts;
  const ParseStatus status = parse_stats_options(argc, argv, opts);
  if (status == ParseStatus::HelpRequested) {
    return kExitOk;
  }
  if (status == ParseStatus::Error) {
    return kExitError;
  }

  const auto file = open_input_file(*opts.file_path);
  if (!file.has_value()) {
    return kExitError;
  }

  std::error_code ec;
  const auto stats = logloom::search_file_parallel(
      *file, logloom::SearchQuery{}, {}, ec, {.num_threads = opts.threads});
  if (ec) {
    std::cerr << "logloom stats: read error: " << ec.message() << '\n';
    return kExitError;
  }

  std::cout << "file: " << opts.file_path->string() << '\n'
            << "size_bytes: " << file->size() << '\n'
            << "lines: " << stats.lines_scanned << '\n'
            << "json_lines: " << stats.json_lines << '\n'
            << "plain_text_lines: " << stats.plain_text_lines << '\n'
            << "malformed_lines: " << stats.malformed_lines << '\n'
            << "oversized_lines: " << stats.oversized_lines << '\n'
            << "truncated_eof: " << (stats.has_truncated_final_line ? "yes" : "no") << '\n';

  return kExitOk;
}

} // namespace logloom::cli
