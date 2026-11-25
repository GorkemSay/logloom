#include "logloom/search_engine.hpp"

#include <algorithm>
#include <regex>

namespace logloom {
namespace {

constexpr unsigned char to_lower_ascii(unsigned char ch) noexcept {
  return (ch >= 'A' && ch <= 'Z')
             ? static_cast<unsigned char>(ch + ('a' - 'A'))
             : ch;
}

[[nodiscard]] bool equals_icase(std::string_view a, std::string_view b) noexcept {
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

[[nodiscard]] bool contains_icase(
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

[[nodiscard]] bool parse_hex4(std::string_view s, std::size_t pos, std::uint32_t& out) noexcept {
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

void append_utf8(std::uint32_t cp, std::string& out) {
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

[[nodiscard]] bool unescape_json_string(std::string_view raw, std::string& out) {
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

} // namespace

SearchStats search_file(
    const PosixFile& file,
    const SearchQuery& query,
    const MatchCallback& on_match,
    std::error_code& ec,
    const std::atomic<bool>* cancel_flag) {
  ec.clear();
  SearchStats stats{};

  if (!file.is_open()) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    return stats;
  }

  std::optional<std::regex> compiled_regex;
  if (query.is_regex && query.text_query.has_value()) {
    auto flags = std::regex::ECMAScript;
    if (!query.case_sensitive) {
      flags |= std::regex::icase;
    }
    try {
      compiled_regex.emplace(*query.text_query, flags);
    } catch (const std::regex_error&) {
      ec = std::make_error_code(std::errc::invalid_argument);
      return stats;
    }
  }

  const JsonlExtractOptions extract_opts{
      .trace_key = query.trace_key,
      .timestamp_key = query.timestamp_key,
      .level_key = query.level_key,
  };

  ChunkedLineReader reader(file, query.reader_options);
  LineView view{};
  std::string unescape_buf{};

  while (true) {
    if (cancel_flag != nullptr && cancel_flag->load(std::memory_order_relaxed)) {
      stats.cancelled = true;
      break;
    }

    if (!reader.next_line(view, ec)) {
      break;
    }

    ++stats.lines_scanned;
    if (view.is_truncated_eof) {
      ++stats.truncated_lines;
      stats.has_truncated_final_line = true;
    }
    if (view.exceeds_max_length) {
      ++stats.oversized_lines;
      continue;
    }

    const ExtractedFields fields = extract_jsonl_fields(view.content, extract_opts);
    switch (fields.kind) {
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

    if (query.trace_id.has_value()) {
      if (fields.kind != LineKind::JsonObject || !fields.trace_id.has_value()) {
        continue;
      }
      if (!fields.has_escapes_in_trace_id) {
        if (*fields.trace_id != *query.trace_id) {
          continue;
        }
      } else {
        if (!unescape_json_string(*fields.trace_id, unescape_buf) ||
            unescape_buf != *query.trace_id) {
          continue;
        }
      }
    }

    if (query.level.has_value()) {
      if (fields.kind != LineKind::JsonObject || !fields.level.has_value() ||
          !equals_icase(*fields.level, *query.level)) {
        continue;
      }
    }

    if (query.text_query.has_value()) {
      bool text_matched = false;
      if (compiled_regex.has_value()) {
        text_matched = std::regex_search(
            view.content.begin(), view.content.end(), *compiled_regex);
      } else if (query.case_sensitive) {
        text_matched = (view.content.find(*query.text_query) != std::string_view::npos);
      } else {
        text_matched = contains_icase(view.content, *query.text_query);
      }
      if (!text_matched) {
        continue;
      }
    }

    ++stats.matches_found;

    if (on_match) {
      const SearchMatch match{
          .line_number = view.line_number,
          .byte_offset = view.byte_offset,
          .byte_length = view.byte_length,
          .content = view.content,
          .fields = fields,
          .has_crlf = view.has_crlf,
          .is_truncated_eof = view.is_truncated_eof,
      };
      if (!on_match(match)) {
        stats.cancelled = true;
        break;
      }
    }

    if (query.limit > 0 && stats.matches_found >= query.limit) {
      stats.limit_reached = true;
      break;
    }
  }

  return stats;
}

} // namespace logloom
