#pragma once

#include "logloom/jsonl_extractor.hpp"
#include "logloom/line_reader.hpp"
#include "logloom/search_engine.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace logloom::detail {

constexpr unsigned char to_lower_ascii(unsigned char ch) noexcept {
  return (ch >= 'A' && ch <= 'Z')
             ? static_cast<unsigned char>(ch + ('a' - 'A'))
             : ch;
}

[[nodiscard]] inline bool equals_icase(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (to_lower_ascii(static_cast<unsigned char>(a[i])) !=
        to_lower_ascii(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] inline bool contains_icase(
    std::string_view haystack,
    std::string_view needle) noexcept {
  if (needle.empty()) {
    return true;
  }
  const auto it = std::search(
      haystack.begin(),
      haystack.end(),
      needle.begin(),
      needle.end(),
      [](char lhs, char rhs) noexcept {
        return to_lower_ascii(static_cast<unsigned char>(lhs)) ==
               to_lower_ascii(static_cast<unsigned char>(rhs));
      });
  return it != haystack.end();
}

[[nodiscard]] inline bool parse_hex4(
    std::string_view s,
    std::size_t pos,
    std::uint32_t& out) noexcept {
  if (pos + 4 > s.size()) {
    return false;
  }
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    const char ch = s[pos + i];
    value <<= 4U;
    if (ch >= '0' && ch <= '9') {
      value |= static_cast<std::uint32_t>(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      value |= static_cast<std::uint32_t>(10 + (ch - 'a'));
    } else if (ch >= 'A' && ch <= 'F') {
      value |= static_cast<std::uint32_t>(10 + (ch - 'A'));
    } else {
      return false;
    }
  }
  out = value;
  return true;
}

inline void append_utf8(std::uint32_t cp, std::string& out) {
  if (cp <= 0x7FU) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FFU) {
    out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else if (cp <= 0xFFFFU) {
    out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  }
}

[[nodiscard]] inline bool unescape_json_string(std::string_view raw, std::string& out) {
  out.clear();
  out.reserve(raw.size());

  for (std::size_t i = 0; i < raw.size(); ++i) {
    const char ch = raw[i];
    if (ch != '\\') {
      out.push_back(ch);
      continue;
    }
    if (i + 1 >= raw.size()) {
      return false;
    }
    const char esc = raw[++i];
    switch (esc) {
      case '"':
      case '\\':
      case '/':
        out.push_back(esc);
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'u': {
        std::uint32_t cp = 0;
        if (!parse_hex4(raw, i + 1, cp)) {
          return false;
        }
        i += 4;
        if (cp >= 0xD800U && cp <= 0xDBFFU && i + 6 < raw.size() &&
            raw[i + 1] == '\\' && raw[i + 2] == 'u') {
          std::uint32_t low = 0;
          if (parse_hex4(raw, i + 3, low) && low >= 0xDC00U && low <= 0xDFFFU) {
            cp = 0x10000U + (((cp - 0xD800U) << 10U) | (low - 0xDC00U));
            i += 6;
          }
        }
        append_utf8(cp, out);
        break;
      }
      default:
        return false;
    }
  }
  return true;
}

[[nodiscard]] inline bool compile_query_regex(
    const SearchQuery& query,
    std::optional<std::regex>& out_regex,
    std::error_code& ec) {
  out_regex.reset();
  if (!query.is_regex || !query.text_query.has_value()) {
    return true;
  }

  auto flags = std::regex::ECMAScript;
  if (!query.case_sensitive) {
    flags |= std::regex::icase;
  }
  try {
    out_regex.emplace(*query.text_query, flags);
  } catch (const std::regex_error&) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }
  return true;
}

[[nodiscard]] inline bool evaluate_line_match(
    std::string_view content,
    const ExtractedFields& fields,
    const SearchQuery& query,
    const std::optional<std::regex>& compiled_regex,
    std::string& unescape_buf) {
  if (query.trace_id.has_value()) {
    if (fields.kind != LineKind::JsonObject || !fields.trace_id.has_value()) {
      return false;
    }
    if (!fields.has_escapes_in_trace_id) {
      if (*fields.trace_id != *query.trace_id) {
        return false;
      }
    } else {
      if (!unescape_json_string(*fields.trace_id, unescape_buf) ||
          unescape_buf != *query.trace_id) {
        return false;
      }
    }
  }

  if (query.level.has_value()) {
    if (fields.kind != LineKind::JsonObject || !fields.level.has_value() ||
        !equals_icase(*fields.level, *query.level)) {
      return false;
    }
  }

  if (query.text_query.has_value()) {
    if (compiled_regex.has_value()) {
      return std::regex_search(content.begin(), content.end(), *compiled_regex);
    }
    if (query.case_sensitive) {
      return content.find(*query.text_query) != std::string_view::npos;
    }
    return contains_icase(content, *query.text_query);
  }

  return true;
}

inline void accumulate_line_kind(LineKind kind, SearchStats& stats) noexcept {
  switch (kind) {
    case LineKind::JsonObject:
      ++stats.json_lines;
      break;
    case LineKind::PlainText:
      ++stats.plain_text_lines;
      break;
    case LineKind::Malformed:
      ++stats.malformed_lines;
      break;
  }
}

inline void merge_partition_stats(const SearchStats& part, SearchStats& total) noexcept {
  total.lines_scanned += part.lines_scanned;
  total.json_lines += part.json_lines;
  total.plain_text_lines += part.plain_text_lines;
  total.malformed_lines += part.malformed_lines;
  total.oversized_lines += part.oversized_lines;
  total.truncated_lines += part.truncated_lines;
  if (part.has_truncated_final_line) {
    total.has_truncated_final_line = true;
  }
}

struct OwnedMatch {
  std::uint64_t local_line_number = 0;
  std::uint64_t byte_offset = 0;
  std::uint64_t byte_length = 0;
  std::string content{};
  bool has_crlf = false;
  bool is_truncated_eof = false;
};

struct PartitionResult {
  std::error_code ec{};
  SearchStats stats{};
  std::vector<OwnedMatch> matches{};
};

struct ThreadPoolGuard {
  std::atomic<bool>& internal_cancel;
  std::condition_variable& cv_work;
  std::vector<std::thread>& threads;

  ~ThreadPoolGuard() {
    internal_cancel.store(true, std::memory_order_release);
    cv_work.notify_all();
    for (auto& t : threads) {
      if (t.joinable()) {
        t.join();
      }
    }
  }
};

} // namespace logloom::detail
