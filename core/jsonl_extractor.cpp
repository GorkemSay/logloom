#include "logloom/jsonl_extractor.hpp"

#include <array>
#include <cstddef>

namespace logloom {
namespace {

constexpr std::size_t kMaxNestingDepth = 512;

constexpr bool is_json_ws(char ch) noexcept {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

constexpr bool is_digit(char ch) noexcept {
  return ch >= '0' && ch <= '9';
}

constexpr bool is_hex_digit(char ch) noexcept {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

struct Cursor {
  std::string_view src;
  std::size_t pos = 0;

  [[nodiscard]] bool eof() const noexcept { return pos >= src.size(); }
  [[nodiscard]] char peek() const noexcept { return src[pos]; }
  void advance() noexcept { ++pos; }

  void skip_ws() noexcept {
    while (pos < src.size() && is_json_ws(src[pos])) {
      ++pos;
    }
  }
};

void consume_digits(Cursor& c) noexcept {
  while (!c.eof() && is_digit(c.peek())) {
    c.advance();
  }
}

[[nodiscard]] bool scan_escape(Cursor& c) noexcept {
  c.advance();
  if (c.eof()) {
    return false;
  }

  const char esc = c.peek();
  c.advance();
  switch (esc) {
    case '"':
    case '\\':
    case '/':
    case 'b':
    case 'f':
    case 'n':
    case 'r':
    case 't':
      return true;
    case 'u':
      for (int i = 0; i < 4; ++i) {
        if (c.eof() || !is_hex_digit(c.peek())) {
          return false;
        }
        c.advance();
      }
      return true;
    default:
      return false;
  }
}

[[nodiscard]] bool scan_string(
    Cursor& c,
    std::string_view& out,
    bool& has_escapes) noexcept {
  if (c.eof() || c.peek() != '"') {
    return false;
  }
  c.advance();

  const std::size_t start = c.pos;
  has_escapes = false;

  while (!c.eof()) {
    const auto byte = static_cast<unsigned char>(c.peek());
    if (byte == '"') {
      out = c.src.substr(start, c.pos - start);
      c.advance();
      return true;
    }
    if (byte == '\\') {
      has_escapes = true;
      if (!scan_escape(c)) {
        return false;
      }
      continue;
    }
    if (byte < 0x20U) {
      return false;
    }
    c.advance();
  }

  return false;
}

[[nodiscard]] bool scan_number(Cursor& c, std::string_view& out) noexcept {
  const std::size_t start = c.pos;

  if (!c.eof() && c.peek() == '-') {
    c.advance();
  }
  if (c.eof()) {
    return false;
  }

  if (c.peek() == '0') {
    c.advance();
  } else if (c.peek() >= '1' && c.peek() <= '9') {
    c.advance();
    consume_digits(c);
  } else {
    return false;
  }

  if (!c.eof() && c.peek() == '.') {
    c.advance();
    if (c.eof() || !is_digit(c.peek())) {
      return false;
    }
    consume_digits(c);
  }

  if (!c.eof() && (c.peek() == 'e' || c.peek() == 'E')) {
    c.advance();
    if (!c.eof() && (c.peek() == '+' || c.peek() == '-')) {
      c.advance();
    }
    if (c.eof() || !is_digit(c.peek())) {
      return false;
    }
    consume_digits(c);
  }

  out = c.src.substr(start, c.pos - start);
  return true;
}

[[nodiscard]] bool scan_literal(Cursor& c, std::string_view& out) noexcept {
  const std::string_view rem = c.src.substr(c.pos);
  if (rem.starts_with("true")) {
    out = rem.substr(0, 4);
    c.pos += 4;
    return true;
  }
  if (rem.starts_with("false")) {
    out = rem.substr(0, 5);
    c.pos += 5;
    return true;
  }
  if (rem.starts_with("null")) {
    out = rem.substr(0, 4);
    c.pos += 4;
    return true;
  }
  return false;
}

enum class FrameExpect : std::uint8_t {
  FirstKeyOrClose,
  Key,
  Colon,
  Value,
  FirstElemOrClose,
  Elem,
  CommaOrClose,
};

struct ContainerFrame {
  bool is_object = false;
  FrameExpect expect = FrameExpect::FirstElemOrClose;
};

[[nodiscard]] bool push_container_frame(
    Cursor& c,
    std::array<ContainerFrame, kMaxNestingDepth>& stack,
    std::size_t& depth) noexcept {
  if (depth >= kMaxNestingDepth) {
    return false;
  }
  const bool is_object = (c.peek() == '{');
  c.advance();
  stack[depth++] = ContainerFrame{
      .is_object = is_object,
      .expect = is_object ? FrameExpect::FirstKeyOrClose : FrameExpect::FirstElemOrClose,
  };
  return true;
}

[[nodiscard]] bool skip_scalar_token(Cursor& c) noexcept {
  std::string_view dummy{};
  bool dummy_escapes = false;
  const char ch = c.peek();
  if (ch == '"') {
    return scan_string(c, dummy, dummy_escapes);
  }
  if (ch == '-' || is_digit(ch)) {
    return scan_number(c, dummy);
  }
  if (ch == 't' || ch == 'f' || ch == 'n') {
    return scan_literal(c, dummy);
  }
  return false;
}

[[nodiscard]] bool skip_nested_container(Cursor& c) noexcept {
  if (c.eof() || (c.peek() != '{' && c.peek() != '[')) {
    return false;
  }

  std::array<ContainerFrame, kMaxNestingDepth> stack{};
  std::size_t depth = 0;
  if (!push_container_frame(c, stack, depth)) {
    return false;
  }

  while (depth > 0) {
    c.skip_ws();
    if (c.eof()) {
      return false;
    }

    ContainerFrame& frame = stack[depth - 1];
    std::string_view dummy{};
    bool dummy_escapes = false;

    switch (frame.expect) {
      case FrameExpect::FirstKeyOrClose:
        if (c.peek() == '}') {
          c.advance();
          --depth;
          break;
        }
        [[fallthrough]];
      case FrameExpect::Key:
        if (!scan_string(c, dummy, dummy_escapes)) {
          return false;
        }
        frame.expect = FrameExpect::Colon;
        break;

      case FrameExpect::Colon:
        if (c.peek() != ':') {
          return false;
        }
        c.advance();
        frame.expect = FrameExpect::Value;
        break;

      case FrameExpect::FirstElemOrClose:
        if (c.peek() == ']') {
          c.advance();
          --depth;
          break;
        }
        [[fallthrough]];
      case FrameExpect::Elem:
      case FrameExpect::Value: {
        frame.expect = FrameExpect::CommaOrClose;
        const char ch = c.peek();
        if (ch == '{' || ch == '[') {
          if (!push_container_frame(c, stack, depth)) {
            return false;
          }
          break;
        }
        if (!skip_scalar_token(c)) {
          return false;
        }
        break;
      }

      case FrameExpect::CommaOrClose: {
        const char close_ch = frame.is_object ? '}' : ']';
        if (c.peek() == ',') {
          c.advance();
          frame.expect = frame.is_object ? FrameExpect::Key : FrameExpect::Elem;
          break;
        }
        if (c.peek() == close_ch) {
          c.advance();
          --depth;
          break;
        }
        return false;
      }
    }
  }

  return true;
}

void assign_scalar_field(
    ExtractedFields& result,
    const JsonlExtractOptions& options,
    std::string_view key,
    bool key_escapes,
    std::optional<std::string_view> value,
    bool val_escapes) noexcept {
  if (key_escapes) {
    return;
  }
  if (!options.timestamp_key.empty() && key == options.timestamp_key) {
    result.timestamp = value;
  }
  if (!options.level_key.empty() && key == options.level_key) {
    result.level = value;
  }
  if (!options.trace_key.empty() && key == options.trace_key) {
    result.trace_id = value;
    result.has_escapes_in_trace_id = value.has_value() ? val_escapes : false;
  }
}

[[nodiscard]] bool parse_top_level_value(
    Cursor& c,
    ExtractedFields& result,
    const JsonlExtractOptions& options,
    std::string_view key,
    bool key_escapes) noexcept {
  const char ch = c.peek();
  if (ch == '{' || ch == '[') {
    return skip_nested_container(c);
  }
  if (ch == '"') {
    std::string_view val{};
    bool val_escapes = false;
    if (!scan_string(c, val, val_escapes)) {
      return false;
    }
    assign_scalar_field(result, options, key, key_escapes, val, val_escapes);
    return true;
  }
  if (ch == '-' || is_digit(ch)) {
    std::string_view val{};
    if (!scan_number(c, val)) {
      return false;
    }
    assign_scalar_field(result, options, key, key_escapes, val, false);
    return true;
  }
  if (ch == 't' || ch == 'f' || ch == 'n') {
    std::string_view val{};
    if (!scan_literal(c, val)) {
      return false;
    }
    const auto scalar = (val == "null") ? std::nullopt : std::optional<std::string_view>(val);
    assign_scalar_field(result, options, key, key_escapes, scalar, false);
    return true;
  }
  return false;
}

} // namespace

ExtractedFields extract_jsonl_fields(
    std::string_view line,
    JsonlExtractOptions options) noexcept {
  Cursor c{line, 0};
  c.skip_ws();

  if (c.eof() || c.peek() != '{') {
    return ExtractedFields{.kind = LineKind::PlainText};
  }
  c.advance();
  c.skip_ws();

  if (c.eof()) {
    return ExtractedFields{.kind = LineKind::Malformed};
  }
  if (c.peek() == '}') {
    c.advance();
    c.skip_ws();
    return ExtractedFields{.kind = c.eof() ? LineKind::JsonObject : LineKind::Malformed};
  }

  ExtractedFields result{};
  result.kind = LineKind::JsonObject;

  while (true) {
    c.skip_ws();
    std::string_view key{};
    bool key_escapes = false;
    if (!scan_string(c, key, key_escapes)) {
      return ExtractedFields{.kind = LineKind::Malformed};
    }

    c.skip_ws();
    if (c.eof() || c.peek() != ':') {
      return ExtractedFields{.kind = LineKind::Malformed};
    }
    c.advance();

    c.skip_ws();
    if (c.eof() || !parse_top_level_value(c, result, options, key, key_escapes)) {
      return ExtractedFields{.kind = LineKind::Malformed};
    }

    c.skip_ws();
    if (c.eof()) {
      return ExtractedFields{.kind = LineKind::Malformed};
    }
    if (c.peek() == ',') {
      c.advance();
      continue;
    }
    if (c.peek() == '}') {
      c.advance();
      c.skip_ws();
      return c.eof() ? result : ExtractedFields{.kind = LineKind::Malformed};
    }
    return ExtractedFields{.kind = LineKind::Malformed};
  }
}

} // namespace logloom
