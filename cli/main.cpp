#include "logloom/line_index.hpp"
#include "logloom/posix_file.hpp"
#include "logloom/search_engine.hpp"
#include "logloom/version.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace {

constexpr int kExitOk = 0;
constexpr int kExitNoMatch = 1;
constexpr int kExitError = 2;

bool should_use_color() noexcept {
  if (::isatty(STDOUT_FILENO) != 1) {
    return false;
  }
  const char* no_color = std::getenv("NO_COLOR");
  return no_color == nullptr || no_color[0] == '\0';
}

void print_global_usage(std::ostream& out) {
  out << "Usage: logloom <command> [options]\n"
      << "       logloom [--help | --version]\n"
      << "\n"
      << "Commands:\n"
      << "  trace    Isolate all lines matching a trace ID\n"
      << "  search   Search log lines by text, regex, or level\n"
      << "  context  Show +/-N surrounding lines around a line number\n"
      << "  stats    Print structural summary and diagnostics for a log file\n"
      << "\n"
      << "Options:\n"
      << "  -h, --help     Show help message and exit\n"
      << "  -V, --version  Show version information and exit\n";
}

void print_trace_usage(std::ostream& out) {
  out << "Usage: logloom trace <file> --id <value> [options]\n"
      << "\n"
      << "Options:\n"
      << "  --id <value>       Trace identifier to isolate (required)\n"
      << "  --key <name>       JSON top-level trace key (default: request_id)\n"
      << "  --context <N>      Include N surrounding lines (default: 0)\n"
      << "  --export <path>    Write raw matching lines to a .jsonl file\n"
      << "  --threads <N>      Worker thread count (default: auto)\n"
      << "  -h, --help         Show this help message\n";
}

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

void print_context_usage(std::ostream& out) {
  out << "Usage: logloom context <file> --line <N> [options]\n"
      << "\n"
      << "Options:\n"
      << "  --line <N>         1-based center line number (required)\n"
      << "  --before <N>       Lines before target (default: 10)\n"
      << "  --after <N>        Lines after target (default: 10)\n"
      << "  -h, --help         Show this help message\n";
}

void print_stats_usage(std::ostream& out) {
  out << "Usage: logloom stats <file>\n"
      << "\n"
      << "Options:\n"
      << "  --threads <N>      Worker thread count (default: auto)\n"
      << "  -h, --help         Show this help message\n";
}

bool parse_u64(std::string_view text, std::uint64_t& out) noexcept {
  if (text.empty()) {
    return false;
  }
  const char* begin = text.data();
  const char* end = begin + text.size();
  std::uint64_t value = 0;
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) {
    return false;
  }
  out = value;
  return true;
}

bool consume_flag_value(
    int argc,
    char* argv[],
    int& index,
    std::string_view flag,
    std::string_view& out_value,
    bool& has_error) {
  const std::string_view arg{argv[index]};
  if (arg == flag) {
    if (index + 1 >= argc) {
      std::cerr << "logloom: option '" << flag << "' requires a value\n";
      has_error = true;
      return true;
    }
    out_value = argv[++index];
    return true;
  }
  if (arg.size() > flag.size() + 1 &&
      arg.starts_with(flag) &&
      arg[flag.size()] == '=') {
    out_value = arg.substr(flag.size() + 1);
    return true;
  }
  return false;
}

void print_formatted_line(
    std::ostream& out,
    std::uint64_t line_number,
    char sep,
    std::string_view content,
    bool use_color) {
  if (use_color) {
    out << "\x1b[2m" << line_number << sep << "\x1b[0m" << content << '\n';
  } else {
    out << line_number << sep << content << '\n';
  }
}

void emit_scan_warnings(const logloom::SearchStats& stats) {
  if (stats.has_truncated_final_line) {
    std::cerr << "warning: input file has a truncated final line (missing trailing newline)\n";
  }
  if (stats.oversized_lines > 0) {
    std::cerr << "warning: skipped " << stats.oversized_lines
              << " line(s) exceeding max line buffer cap\n";
  }
}

std::optional<logloom::PosixFile> open_input_file(const std::filesystem::path& path) {
  std::error_code ec;
  auto file = logloom::PosixFile::open_read_only(path, ec);
  if (!file.has_value()) {
    std::cerr << "logloom: cannot open '" << path.string() << "': " << ec.message() << '\n';
  }
  return file;
}

struct LineRange {
  std::uint64_t start = 0;
  std::uint64_t end = 0;
};

int run_trace(int argc, char* argv[], bool use_color) {
  std::optional<std::filesystem::path> file_path;
  std::optional<std::string> trace_id;
  std::string trace_key = "request_id";
  std::uint64_t context_lines = 0;
  std::optional<std::filesystem::path> export_path;
  std::size_t threads = 0;

  for (int i = 2; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    std::string_view val;
    bool flag_err = false;

    if (arg == "-h" || arg == "--help") {
      print_trace_usage(std::cout);
      return kExitOk;
    }
    if (consume_flag_value(argc, argv, i, "--id", val, flag_err)) {
      if (flag_err) {
        return kExitError;
      }
      trace_id = std::string(val);
    } else if (consume_flag_value(argc, argv, i, "--key", val, flag_err)) {
      if (flag_err) {
        return kExitError;
      }
      if (val.empty()) {
        std::cerr << "logloom: --key cannot be empty\n";
        return kExitError;
      }
      trace_key = std::string(val);
    } else if (consume_flag_value(argc, argv, i, "--context", val, flag_err)) {
      if (flag_err) {
        return kExitError;
      }
      if (!parse_u64(val, context_lines)) {
        std::cerr << "logloom: invalid integer for --context: '" << val << "'\n";
        return kExitError;
      }
    } else if (consume_flag_value(argc, argv, i, "--export", val, flag_err)) {
      if (flag_err) {
        return kExitError;
      }
      if (val.empty()) {
        std::cerr << "logloom: --export path cannot be empty\n";
        return kExitError;
      }
      export_path = std::filesystem::path(val);
    } else if (consume_flag_value(argc, argv, i, "--threads", val, flag_err)) {
      if (flag_err) {
        return kExitError;
      }
      std::uint64_t parsed = 0;
      if (!parse_u64(val, parsed) || parsed == 0) {
        std::cerr << "logloom: --threads must be a positive integer\n";
        return kExitError;
      }
      threads = static_cast<std::size_t>(parsed);
    } else if (arg.starts_with("-")) {
      std::cerr << "logloom trace: unknown option '" << arg << "'\n";
      return kExitError;
    } else if (!file_path.has_value()) {
      file_path = std::filesystem::path(arg);
    } else {
      std::cerr << "logloom trace: unexpected positional argument '" << arg << "'\n";
      return kExitError;
    }
  }

  if (!file_path.has_value() || !trace_id.has_value()) {
    std::cerr << "logloom trace: <file> and --id <value> are required\n";
    return kExitError;
  }

  const auto file = open_input_file(*file_path);
  if (!file.has_value()) {
    return kExitError;
  }

  std::optional<std::ofstream> export_stream;
  if (export_path.has_value()) {
    std::error_code fs_ec;
    if (std::filesystem::exists(*export_path, fs_ec) && !fs_ec &&
        std::filesystem::exists(*file_path, fs_ec) && !fs_ec &&
        std::filesystem::equivalent(*file_path, *export_path, fs_ec)) {
      std::cerr << "logloom: refusing to overwrite input log file via --export\n";
      return kExitError;
    }
    export_stream.emplace(*export_path, std::ios::binary | std::ios::trunc);
    if (!export_stream->is_open()) {
      std::cerr << "logloom: cannot open export file '" << export_path->string() << "'\n";
      return kExitError;
    }
  }

  logloom::SearchQuery query;
  query.trace_id = *trace_id;
  query.trace_key = trace_key;

  std::vector<std::uint64_t> matched_lines;
  bool write_failed = false;

  std::error_code ec;
  const auto stats = logloom::search_file_parallel(
      *file,
      query,
      [&](const logloom::SearchMatch& match) {
        if (export_stream.has_value()) {
          export_stream->write(
              match.content.data(),
              static_cast<std::streamsize>(match.content.size()));
          export_stream->put('\n');
          if (!export_stream->good()) {
            write_failed = true;
            return false;
          }
        }
        if (context_lines > 0) {
          matched_lines.push_back(match.line_number);
        } else {
          print_formatted_line(std::cout, match.line_number, ':', match.content, use_color);
        }
        return true;
      },
      ec,
      {.num_threads = threads});

  if (write_failed) {
    std::cerr << "logloom: failed writing export file '" << export_path->string() << "'\n";
    return kExitError;
  }
  if (ec) {
    std::cerr << "logloom trace: read error: " << ec.message() << '\n';
    return kExitError;
  }

  if (export_stream.has_value()) {
    export_stream->flush();
    if (!export_stream->good()) {
      std::cerr << "logloom: failed flushing export file '" << export_path->string() << "'\n";
      return kExitError;
    }
  }

  if (context_lines > 0 && !matched_lines.empty()) {
    const auto index = logloom::LineIndex::build(*file, ec);
    if (!index.has_value()) {
      std::cerr << "logloom trace: failed to build line index: " << ec.message() << '\n';
      return kExitError;
    }

    const std::uint64_t total = index->line_count();
    std::vector<LineRange> ranges;
    ranges.reserve(matched_lines.size());
    for (const std::uint64_t ln : matched_lines) {
      const std::uint64_t start = (context_lines >= ln - 1) ? 1ULL : (ln - context_lines);
      const std::uint64_t end =
          (context_lines >= total - ln) ? total : (ln + context_lines);
      if (!ranges.empty() && start <= ranges.back().end + 1ULL) {
        ranges.back().end = std::max(ranges.back().end, end);
      } else {
        ranges.push_back(LineRange{start, end});
      }
    }

    std::size_t match_cursor = 0;
    std::vector<logloom::IndexedLine> ctx;
    for (std::size_t r = 0; r < ranges.size(); ++r) {
      if (r > 0) {
        std::cout << "--\n";
      }
      const std::uint64_t after = ranges[r].end - ranges[r].start;
      if (!index->read_context(*file, ranges[r].start, 0, after, ctx, ec)) {
        std::cerr << "logloom trace: context seek error: " << ec.message() << '\n';
        return kExitError;
      }
      for (const auto& line : ctx) {
        while (match_cursor < matched_lines.size() &&
               matched_lines[match_cursor] < line.line_number) {
          ++match_cursor;
        }
        const bool is_match = (match_cursor < matched_lines.size() &&
                               matched_lines[match_cursor] == line.line_number);
        const char sep = is_match ? ':' : '-';
        if (line.exceeds_max_length) {
          const std::string msg =
              "[oversized line: " + std::to_string(line.byte_length) + " bytes skipped]";
          print_formatted_line(std::cout, line.line_number, sep, msg, use_color);
        } else {
          print_formatted_line(std::cout, line.line_number, sep, line.content, use_color);
        }
      }
    }
  }

  emit_scan_warnings(stats);
  return stats.matches_found > 0 ? kExitOk : kExitNoMatch;
}

int run_search(int argc, char* argv[], bool use_color) {
  std::optional<std::filesystem::path> file_path;
  logloom::SearchQuery query;
  std::size_t threads = 0;

  for (int i = 2; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    std::string_view val;
    bool flag_err = false;

    if (arg == "-h" || arg == "--help") {
      print_search_usage(std::cout);
      return kExitOk;
    }
    if (arg == "--regex") {
      query.is_regex = true;
    } else if (arg == "-i" || arg == "--ignore-case") {
      query.case_sensitive = false;
    } else if (consume_flag_value(argc, argv, i, "--query", val, flag_err)) {
      if (flag_err) {
        return kExitError;
      }
      query.text_query = std::string(val);
    } else if (consume_flag_value(argc, argv, i, "--level", val, flag_err)) {
      if (flag_err) {
        return kExitError;
      }
      query.level = std::string(val);
    } else if (consume_flag_value(argc, argv, i, "--limit", val, flag_err)) {
      if (flag_err) {
        return kExitError;
      }
      if (!parse_u64(val, query.limit) || query.limit == 0) {
        std::cerr << "logloom: --limit must be a positive integer\n";
        return kExitError;
      }
    } else if (consume_flag_value(argc, argv, i, "--threads", val, flag_err)) {
      if (flag_err) {
        return kExitError;
      }
      std::uint64_t parsed = 0;
      if (!parse_u64(val, parsed) || parsed == 0) {
        std::cerr << "logloom: --threads must be a positive integer\n";
        return kExitError;
      }
      threads = static_cast<std::size_t>(parsed);
    } else if (arg.starts_with("-")) {
      std::cerr << "logloom search: unknown option '" << arg << "'\n";
      return kExitError;
    } else if (!file_path.has_value()) {
      file_path = std::filesystem::path(arg);
    } else {
      std::cerr << "logloom search: unexpected positional argument '" << arg << "'\n";
      return kExitError;
    }
  }

  if (!file_path.has_value() || (!query.text_query.has_value() && !query.level.has_value())) {
    std::cerr << "logloom search: <file> and at least --query or --level are required\n";
    return kExitError;
  }

  const auto file = open_input_file(*file_path);
  if (!file.has_value()) {
    return kExitError;
  }

  std::error_code ec;
  const auto stats = logloom::search_file_parallel(
      *file,
      query,
      [&](const logloom::SearchMatch& match) {
        print_formatted_line(std::cout, match.line_number, ':', match.content, use_color);
        return true;
      },
      ec,
      {.num_threads = threads});

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

int run_context(int argc, char* argv[], bool use_color) {
  std::optional<std::filesystem::path> file_path;
  std::optional<std::uint64_t> target_line;
  std::uint64_t before = 10;
  std::uint64_t after = 10;

  for (int i = 2; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    std::string_view val;
    bool flag_err = false;

    if (arg == "-h" || arg == "--help") {
      print_context_usage(std::cout);
      return kExitOk;
    }
    if (consume_flag_value(argc, argv, i, "--line", val, flag_err)) {
      if (flag_err) {
        return kExitError;
      }
      std::uint64_t parsed = 0;
      if (!parse_u64(val, parsed) || parsed == 0) {
        std::cerr << "logloom: --line must be a positive 1-based integer\n";
        return kExitError;
      }
      target_line = parsed;
    } else if (consume_flag_value(argc, argv, i, "--before", val, flag_err)) {
      if (flag_err) {
        return kExitError;
      }
      if (!parse_u64(val, before)) {
        std::cerr << "logloom: invalid integer for --before: '" << val << "'\n";
        return kExitError;
      }
    } else if (consume_flag_value(argc, argv, i, "--after", val, flag_err)) {
      if (flag_err) {
        return kExitError;
      }
      if (!parse_u64(val, after)) {
        std::cerr << "logloom: invalid integer for --after: '" << val << "'\n";
        return kExitError;
      }
    } else if (arg.starts_with("-")) {
      std::cerr << "logloom context: unknown option '" << arg << "'\n";
      return kExitError;
    } else if (!file_path.has_value()) {
      file_path = std::filesystem::path(arg);
    } else {
      std::cerr << "logloom context: unexpected positional argument '" << arg << "'\n";
      return kExitError;
    }
  }

  if (!file_path.has_value() || !target_line.has_value()) {
    std::cerr << "logloom context: <file> and --line <N> are required\n";
    return kExitError;
  }

  const auto file = open_input_file(*file_path);
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
  if (!index->read_context(*file, *target_line, before, after, ctx, ec)) {
    if (ec) {
      std::cerr << "logloom context: read error: " << ec.message() << '\n';
      return kExitError;
    }
    std::cerr << "logloom context: line " << *target_line
              << " is out of range (total lines: " << index->line_count() << ")\n";
    return kExitNoMatch;
  }

  for (const auto& line : ctx) {
    const char sep = (line.line_number == *target_line) ? ':' : '-';
    if (line.exceeds_max_length) {
      const std::string msg =
          "[oversized line: " + std::to_string(line.byte_length) + " bytes skipped]";
      print_formatted_line(std::cout, line.line_number, sep, msg, use_color);
    } else {
      print_formatted_line(std::cout, line.line_number, sep, line.content, use_color);
    }
  }

  return kExitOk;
}

int run_stats(int argc, char* argv[]) {
  std::optional<std::filesystem::path> file_path;
  std::size_t threads = 0;

  for (int i = 2; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    std::string_view val;
    bool flag_err = false;

    if (arg == "-h" || arg == "--help") {
      print_stats_usage(std::cout);
      return kExitOk;
    }
    if (consume_flag_value(argc, argv, i, "--threads", val, flag_err)) {
      if (flag_err) {
        return kExitError;
      }
      std::uint64_t parsed = 0;
      if (!parse_u64(val, parsed) || parsed == 0) {
        std::cerr << "logloom: --threads must be a positive integer\n";
        return kExitError;
      }
      threads = static_cast<std::size_t>(parsed);
    } else if (arg.starts_with("-")) {
      std::cerr << "logloom stats: unknown option '" << arg << "'\n";
      return kExitError;
    } else if (!file_path.has_value()) {
      file_path = std::filesystem::path(arg);
    } else {
      std::cerr << "logloom stats: unexpected positional argument '" << arg << "'\n";
      return kExitError;
    }
  }

  if (!file_path.has_value()) {
    std::cerr << "logloom stats: <file> is required\n";
    return kExitError;
  }

  const auto file = open_input_file(*file_path);
  if (!file.has_value()) {
    return kExitError;
  }

  std::error_code ec;
  const auto stats = logloom::search_file_parallel(
      *file, logloom::SearchQuery{}, {}, ec, {.num_threads = threads});
  if (ec) {
    std::cerr << "logloom stats: read error: " << ec.message() << '\n';
    return kExitError;
  }

  std::cout << "file: " << file_path->string() << '\n'
            << "size_bytes: " << file->size() << '\n'
            << "lines: " << stats.lines_scanned << '\n'
            << "json_lines: " << stats.json_lines << '\n'
            << "plain_text_lines: " << stats.plain_text_lines << '\n'
            << "malformed_lines: " << stats.malformed_lines << '\n'
            << "oversized_lines: " << stats.oversized_lines << '\n'
            << "truncated_eof: " << (stats.has_truncated_final_line ? "yes" : "no") << '\n';

  return kExitOk;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc <= 1) {
    print_global_usage(std::cout);
    return kExitOk;
  }

  const std::string_view cmd{argv[1]};
  if (cmd == "--version" || cmd == "-V") {
    std::cout << "logloom " << logloom::version() << '\n';
    return kExitOk;
  }
  if (cmd == "--help" || cmd == "-h") {
    print_global_usage(std::cout);
    return kExitOk;
  }

  const bool use_color = should_use_color();

  if (cmd == "trace") {
    return run_trace(argc, argv, use_color);
  }
  if (cmd == "search") {
    return run_search(argc, argv, use_color);
  }
  if (cmd == "context") {
    return run_context(argc, argv, use_color);
  }
  if (cmd == "stats") {
    return run_stats(argc, argv);
  }

  std::cerr << "logloom: unknown command or option '" << cmd << "'\n"
            << "Try 'logloom --help' for usage.\n";
  return kExitError;
}
