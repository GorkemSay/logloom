#include "arg_parser.hpp"
#include "cli_common.hpp"
#include "commands.hpp"

#include "logloom/search_engine.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>

namespace logloom::cli {
namespace {

struct SearchOptions {
  std::optional<std::filesystem::path> file_path;
  logloom::SearchQuery query;
  std::size_t threads = 0;
};

void print_search_usage(std::ostream& out) {
  out << "Usage: logloom search <file> [options]\n"
      << "\n"
      << "Options:\n"
      << "  --query <text>     Text or regex pattern to search for\n"
      << "  --regex            Interpret --query as ECMAScript regex\n"
      << "  -i, --ignore-case  Case-insensitive text/regex matching\n"
      << "  --level <LEVEL>    Filter JSONL lines by level (case-insensitive)\n"
      << "  --limit <N>        Stop after N matches\n"
      << "  --threads <N>      Worker thread count (default: auto)\n"
      << "  -h, --help         Show this help message\n";
}

ParseStatus parse_search_options(int argc, char* argv[], SearchOptions& opts) {
  ArgParser parser("search", print_search_usage);
  parser.add_positional_file(opts.file_path);

  parser.add_flag("--regex", "", [&]() { opts.query.is_regex = true; });
  parser.add_flag("--ignore-case", "-i", [&]() { opts.query.case_sensitive = false; });

  parser.add_string_option("--query", [&](std::string_view val) {
    opts.query.text_query = std::string(val);
    return true;
  });
  parser.add_string_option("--level", [&](std::string_view val) {
    opts.query.level = std::string(val);
    return true;
  });
  parser.add_u64_option(
      "--limit",
      true,
      [&](std::uint64_t parsed) { opts.query.limit = parsed; },
      "logloom: --limit must be a positive integer\n");
  parser.add_u64_option(
      "--threads",
      true,
      [&](std::uint64_t parsed) { opts.threads = static_cast<std::size_t>(parsed); },
      "logloom: --threads must be a positive integer\n");

  const ParseStatus status = parser.parse(argc, argv);
  if (status != ParseStatus::Ok) {
    return status;
  }
  if (!opts.file_path.has_value() ||
      (!opts.query.text_query.has_value() && !opts.query.level.has_value())) {
    std::cerr << "logloom search: <file> and at least --query or --level are required\n";
    return ParseStatus::Error;
  }
  return ParseStatus::Ok;
}

} // namespace

int run_search(int argc, char* argv[], bool use_color) {
  SearchOptions opts;
  const ParseStatus status = parse_search_options(argc, argv, opts);
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
      *file,
      opts.query,
      [&](const logloom::SearchMatch& match) {
        print_formatted_line(std::cout, match.line_number, ':', match.content, use_color);
        return true;
      },
      ec,
      {.num_threads = opts.threads});

  if (ec) {
    if (ec == std::errc::invalid_argument) {
      std::cerr << "logloom search: invalid regular expression\n";
    } else {
      std::cerr << "logloom search: read error: " << ec.message() << '\n';
    }
    return kExitError;
  }

  emit_scan_warnings(stats);
  return stats.matches_found > 0 ? kExitOk : kExitNoMatch;
}

} // namespace logloom::cli
