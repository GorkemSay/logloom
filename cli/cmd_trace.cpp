#include "arg_parser.hpp"
#include "cli_common.hpp"
#include "commands.hpp"

#include "logloom/line_index.hpp"
#include "logloom/search_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace logloom::cli {
namespace {

struct TraceOptions {
  std::optional<std::filesystem::path> file_path;
  std::optional<std::string> trace_id;
  std::string trace_key = "request_id";
  std::uint64_t context_lines = 0;
  std::optional<std::filesystem::path> export_path;
  std::size_t threads = 0;
};

struct LineRange {
  std::uint64_t start = 0;
  std::uint64_t end = 0;
};

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

ParseStatus parse_trace_options(int argc, char* argv[], TraceOptions& opts) {
  ArgParser parser("trace", print_trace_usage);
  parser.add_positional_file(opts.file_path);

  parser.add_string_option("--id", [&](std::string_view val) {
    opts.trace_id = std::string(val);
    return true;
  });
  parser.add_string_option("--key", [&](std::string_view val) {
    if (val.empty()) {
      std::cerr << "logloom: --key cannot be empty\n";
      return false;
    }
    opts.trace_key = std::string(val);
    return true;
  });
  parser.add_string_option("--context", [&](std::string_view val) {
    if (!parse_u64(val, opts.context_lines)) {
      std::cerr << "logloom: invalid integer for --context: '" << val << "'\n";
      return false;
    }
    return true;
  });
  parser.add_string_option("--export", [&](std::string_view val) {
    if (val.empty()) {
      std::cerr << "logloom: --export path cannot be empty\n";
      return false;
    }
    opts.export_path = std::filesystem::path(val);
    return true;
  });
  parser.add_u64_option(
      "--threads",
      true,
      [&](std::uint64_t parsed) { opts.threads = static_cast<std::size_t>(parsed); },
      "logloom: --threads must be a positive integer\n");

  const ParseStatus status = parser.parse(argc, argv);
  if (status != ParseStatus::Ok) {
    return status;
  }
  if (!opts.file_path.has_value() || !opts.trace_id.has_value()) {
    std::cerr << "logloom trace: <file> and --id <value> are required\n";
    return ParseStatus::Error;
  }
  return ParseStatus::Ok;
}

bool open_export_stream(
    const std::filesystem::path& input_path,
    const std::filesystem::path& export_path,
    std::optional<std::ofstream>& out_stream) {
  std::error_code fs_ec;
  const bool same_target =
      std::filesystem::exists(export_path, fs_ec) && !fs_ec &&
      std::filesystem::exists(input_path, fs_ec) && !fs_ec &&
      std::filesystem::equivalent(input_path, export_path, fs_ec);
  if (same_target) {
    std::cerr << "logloom: refusing to overwrite input log file via --export\n";
    return false;
  }

  out_stream.emplace(export_path, std::ios::binary | std::ios::trunc);
  if (!out_stream->is_open()) {
    std::cerr << "logloom: cannot open export file '" << export_path.string() << "'\n";
    out_stream.reset();
    return false;
  }
  return true;
}

std::vector<LineRange> build_merged_ranges(
    const std::vector<std::uint64_t>& matched_lines,
    std::uint64_t context_lines,
    std::uint64_t total_lines) {
  std::vector<LineRange> ranges;
  if (total_lines == 0) {
    return ranges;
  }
  ranges.reserve(matched_lines.size());
  for (const std::uint64_t ln : matched_lines) {
    const std::uint64_t start = (context_lines >= ln - 1) ? 1ULL : (ln - context_lines);
    const std::uint64_t end =
        (ln >= total_lines || context_lines >= total_lines - ln)
            ? total_lines
            : (ln + context_lines);
    if (!ranges.empty() && start <= ranges.back().end + 1ULL) {
      ranges.back().end = std::max(ranges.back().end, end);
    } else {
      ranges.push_back(LineRange{start, end});
    }
  }
  return ranges;
}

bool render_context_hunks(
    const logloom::PosixFile& file,
    const logloom::LineIndex& index,
    const std::vector<std::uint64_t>& matched_lines,
    std::uint64_t context_lines,
    bool use_color) {
  const auto ranges = build_merged_ranges(matched_lines, context_lines, index.line_count());
  std::size_t match_cursor = 0;
  std::vector<logloom::IndexedLine> ctx;
  std::error_code ec;

  for (std::size_t r = 0; r < ranges.size(); ++r) {
    if (r > 0) {
      std::cout << "--\n";
    }
    const std::uint64_t after = ranges[r].end - ranges[r].start;
    if (!index.read_context(file, ranges[r].start, 0, after, ctx, ec)) {
      std::cerr << "logloom trace: context seek error: " << ec.message() << '\n';
      return false;
    }
    for (const auto& line : ctx) {
      while (match_cursor < matched_lines.size() &&
             matched_lines[match_cursor] < line.line_number) {
        ++match_cursor;
      }
      const bool is_match = (match_cursor < matched_lines.size() &&
                             matched_lines[match_cursor] == line.line_number);
      print_indexed_line(std::cout, line, is_match ? ':' : '-', use_color);
    }
  }
  return true;
}

} // namespace

int run_trace(int argc, char* argv[], bool use_color) {
  TraceOptions opts;
  const ParseStatus status = parse_trace_options(argc, argv, opts);
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

  std::optional<std::ofstream> export_stream;
  if (opts.export_path.has_value() &&
      !open_export_stream(*opts.file_path, *opts.export_path, export_stream)) {
    return kExitError;
  }

  logloom::SearchQuery query;
  query.trace_id = *opts.trace_id;
  query.trace_key = opts.trace_key;

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
        if (opts.context_lines > 0) {
          matched_lines.push_back(match.line_number);
        } else {
          print_formatted_line(std::cout, match.line_number, ':', match.content, use_color);
        }
        return true;
      },
      ec,
      {.num_threads = opts.threads});

  if (write_failed) {
    std::cerr << "logloom: failed writing export file '" << opts.export_path->string() << "'\n";
    return kExitError;
  }
  if (ec) {
    std::cerr << "logloom trace: read error: " << ec.message() << '\n';
    return kExitError;
  }

  if (export_stream.has_value()) {
    export_stream->flush();
    if (!export_stream->good()) {
      std::cerr << "logloom: failed flushing export file '" << opts.export_path->string() << "'\n";
      return kExitError;
    }
  }

  if (opts.context_lines > 0 && !matched_lines.empty()) {
    const auto index = logloom::LineIndex::build(*file, ec);
    if (!index.has_value()) {
      std::cerr << "logloom trace: failed to build line index: " << ec.message() << '\n';
      return kExitError;
    }
    if (!render_context_hunks(*file, *index, matched_lines, opts.context_lines, use_color)) {
      return kExitError;
    }
  }

  emit_scan_warnings(stats);
  return stats.matches_found > 0 ? kExitOk : kExitNoMatch;
}

} // namespace logloom::cli
