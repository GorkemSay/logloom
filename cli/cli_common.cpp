#include "cli_common.hpp"

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <string>
#include <system_error>
#include <unistd.h>

namespace logloom::cli {

bool should_use_color() noexcept {
  if (::isatty(STDOUT_FILENO) != 1) {
    return false;
  }
  const char* no_color = std::getenv("NO_COLOR");
  return no_color == nullptr || no_color[0] == '\0';
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

void print_formatted_line(
    std::ostream& out,
    std::uint64_t line_number,
    char sep,
    std::string_view content,
    bool use_color) {
  if (use_color) {
    out << "\x1b[2m" << line_number << sep << "\x1b[0m" << content << '\n';
    return;
  }
  out << line_number << sep << content << '\n';
}

void print_indexed_line(
    std::ostream& out,
    const logloom::IndexedLine& line,
    char sep,
    bool use_color) {
  if (!line.exceeds_max_length) {
    print_formatted_line(out, line.line_number, sep, line.content, use_color);
    return;
  }
  const std::string msg =
      "[oversized line: " + std::to_string(line.byte_length) + " bytes skipped]";
  print_formatted_line(out, line.line_number, sep, msg, use_color);
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

} // namespace logloom::cli
