#include "arg_parser.hpp"
#include "cli_common.hpp"
#include "commands.hpp"

#include "logloom/line_index.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <system_error>
#include <vector>

namespace logloom::cli {
namespace {

struct ContextOptions {
  std::optional<std::filesystem::path> file_path;
  std::optional<std::uint64_t> target_line;
  std::uint64_t before = 10;
  std::uint64_t after = 10;
};

void print_context_usage(std::ostream& out) {
  out << "Usage: logloom context <file> --line <N> [options]\n"
      << "\n"
      << "Options:\n"
      << "  --line <N>         1-based center line number (required)\n"
      << "  --before <N>       Lines before target (default: 10)\n"
      << "  --after <N>        Lines after target (default: 10)\n"
      << "  -h, --help         Show this help message\n";
}

ParseStatus parse_context_options(int argc, char* argv[], ContextOptions& opts) {
  ArgParser parser("context", print_context_usage);
  parser.add_positional_file(opts.file_path);

  parser.add_u64_option(
      "--line",
      true,
      [&](std::uint64_t parsed) { opts.target_line = parsed; },
      "logloom: --line must be a positive 1-based integer\n");
  parser.add_string_option("--before", [&](std::string_view val) {
    if (!parse_u64(val, opts.before)) {
      std::cerr << "logloom: invalid integer for --before: '" << val << "'\n";
      return false;
    }
    return true;
  });
  parser.add_string_option("--after", [&](std::string_view val) {
    if (!parse_u64(val, opts.after)) {
      std::cerr << "logloom: invalid integer for --after: '" << val << "'\n";
      return false;
    }
    return true;
  });

  const ParseStatus status = parser.parse(argc, argv);
  if (status != ParseStatus::Ok) {
    return status;
  }
  if (!opts.file_path.has_value() || !opts.target_line.has_value()) {
    std::cerr << "logloom context: <file> and --line <N> are required\n";
    return ParseStatus::Error;
  }
  return ParseStatus::Ok;
}

} // namespace

int run_context(int argc, char* argv[], bool use_color) {
  ContextOptions opts;
  const ParseStatus status = parse_context_options(argc, argv, opts);
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
  const auto index = logloom::LineIndex::build(*file, ec);
  if (!index.has_value()) {
    std::cerr << "logloom context: failed to index file: " << ec.message() << '\n';
    return kExitError;
  }

  std::vector<logloom::IndexedLine> ctx;
  if (!index->read_context(*file, *opts.target_line, opts.before, opts.after, ctx, ec)) {
    if (ec) {
      std::cerr << "logloom context: read error: " << ec.message() << '\n';
      return kExitError;
    }
    std::cerr << "logloom context: line " << *opts.target_line
              << " is out of range (total lines: " << index->line_count() << ")\n";
    return kExitNoMatch;
  }

  for (const auto& line : ctx) {
    const char sep = (line.line_number == *opts.target_line) ? ':' : '-';
    print_indexed_line(std::cout, line, sep, use_color);
  }

  return kExitOk;
}

} // namespace logloom::cli
