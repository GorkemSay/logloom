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
          break;
        case 'u':
          for (int i = 0; i < 4; ++i) {
            if (c.eof() || !is_hex_digit(c.peek())) {
              return false;
            }
            c.advance();
          }
          break;
        default:
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
    while (!c.eof() && is_digit(c.peek())) {
      c.advance();
    }
  } else {
    return false;
  }

  if (!c.eof() && c.peek() == '.') {
    c.advance();
    if (c.eof() || !is_digit(c.peek())) {
      return false;
    }
    while (!c.eof() && is_digit(c.peek())) {
      c.advance();
    }
  }

  if (!c.eof() && (c.peek() == 'e' || c.peek() == 'E')) {
    c.advance();
    if (!c.eof() && (c.peek() == '+' || c.peek() == '-')) {
      c.advance();
    }
    if (c.eof() || !is_digit(c.peek())) {
      return false;
    }
    while (!c.eof() && is_digit(c.peek())) {
      c.advance();
    }
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

[[nodiscard]] bool skip_nested_container(Cursor& c) noexcept {
  if (c.eof() || (c.peek() != '{' && c.peek() != '[')) {
    return false;
  }

  std::array<ContainerFrame, kMaxNestingDepth> stack{};
  std::size_t depth = 0;

  const bool root_is_obj = (c.peek() == '{');
  c.advance();
  stack[depth++] = ContainerFrame{
      .is_object = root_is_obj,
      .expect = root_is_obj ? FrameExpect::FirstKeyOrClose : FrameExpect::FirstElemOrClose,
  };

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
          if (depth >= kMaxNestingDepth) {
            return false;
          }
          const bool child_is_obj = (ch == '{');
          c.advance();
          stack[depth++] = ContainerFrame{
              .is_object = child_is_obj,
              .expect = child_is_obj ? FrameExpect::FirstKeyOrClose
                                     : FrameExpect::FirstElemOrClose,
          };
        } else if (ch == '"') {
          if (!scan_string(c, dummy, dummy_escapes)) {
            return false;
          }
        } else if (ch == '-' || is_digit(ch)) {
          if (!scan_number(c, dummy)) {
            return false;
          }
        } else if (ch == 't' || ch == 'f' || ch == 'n') {
          if (!scan_literal(c, dummy)) {
            return false;
          }
        } else {
          return false;
        }
        break;
      }

      case FrameExpect::CommaOrClose: {
        const char close_ch = frame.is_object ? '}' : ']';
        if (c.peek() == ',') {
          c.advance();
          frame.expect = frame.is_object ? FrameExpect::Key : FrameExpect::Elem;
        } else if (c.peek() == close_ch) {
          c.advance();
          --depth;
        } else {
          return false;
        }
        break;
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
    return ExtractedFields{
        .kind = c.eof() ? LineKind::JsonObject : LineKind::Malformed,
    };
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
    if (c.eof()) {
      return ExtractedFields{.kind = LineKind::Malformed};
    }

    const char ch = c.peek();
    if (ch == '{' || ch == '[') {
      if (!skip_nested_container(c)) {
        return ExtractedFields{.kind = LineKind::Malformed};
      }
    } else if (ch == '"') {
      std::string_view val{};
      bool val_escapes = false;
      if (!scan_string(c, val, val_escapes)) {
        return ExtractedFields{.kind = LineKind::Malformed};
      }
      assign_scalar_field(result, options, key, key_escapes, val, val_escapes);
    } else if (ch == '-' || is_digit(ch)) {
      std::string_view val{};
      if (!scan_number(c, val)) {
        return ExtractedFields{.kind = LineKind::Malformed};
      }
      assign_scalar_field(result, options, key, key_escapes, val, false);
    } else if (ch == 't' || ch == 'f' || ch == 'n') {
      std::string_view val{};
      if (!scan_literal(c, val)) {
        return ExtractedFields{.kind = LineKind::Malformed};
      }
      if (val == "null") {
        assign_scalar_field(result, options, key, key_escapes, std::nullopt, false);
      } else {
        assign_scalar_field(result, options, key, key_escapes, val, false);
      }
    } else {
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
      if (!c.eof()) {
        return ExtractedFields{.kind = LineKind::Malformed};
      }
      return result;
    }
    return ExtractedFields{.kind = LineKind::Malformed};
  }
}

} // namespace logloom
