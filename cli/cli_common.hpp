#pragma once

#include "logloom/line_index.hpp"
#include "logloom/posix_file.hpp"
#include "logloom/search_engine.hpp"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string_view>

namespace logloom::cli {

inline constexpr int kExitOk = 0;
inline constexpr int kExitNoMatch = 1;
inline constexpr int kExitError = 2;

[[nodiscard]] bool should_use_color() noexcept;
[[nodiscard]] bool parse_u64(std::string_view text, std::uint64_t& out) noexcept;

void print_formatted_line(
    std::ostream& out,
    std::uint64_t line_number,
    char sep,
    std::string_view content,
    bool use_color);

void print_indexed_line(
    std::ostream& out,
    const logloom::IndexedLine& line,
    char sep,
    bool use_color);

void emit_scan_warnings(const logloom::SearchStats& stats);

[[nodiscard]] std::optional<logloom::PosixFile> open_input_file(
    const std::filesystem::path& path);

} // namespace logloom::cli
