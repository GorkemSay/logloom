#include "logloom/jsonl_extractor.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace {

bool is_zero_copy_slice(std::string_view whole, std::string_view slice) noexcept {
  if (slice.empty()) {
    return true;
  }
  const char* begin = whole.data();
  const char* end = whole.data() + whole.size();
  return slice.data() >= begin && (slice.data() + slice.size()) <= end;
}

} // namespace

TEST_CASE("extract_jsonl_fields parses standard JSONL lines as zero-copy views", "[jsonl]") {
  const std::string line =
      "  \t {\"timestamp\": \"2026-11-25T12:00:00.123Z\", \"level\" : \"ERROR\", "
      "\"request_id\":\"req-9941\",\"msg\":\"db timeout\"} \r\n ";

  const auto res = logloom::extract_jsonl_fields(line);
  REQUIRE(res.kind == logloom::LineKind::JsonObject);
  REQUIRE(res.timestamp == "2026-11-25T12:00:00.123Z");
  REQUIRE(res.level == "ERROR");
  REQUIRE(res.trace_id == "req-9941");
  REQUIRE_FALSE(res.has_escapes_in_trace_id);

  REQUIRE(is_zero_copy_slice(line, *res.timestamp));
  REQUIRE(is_zero_copy_slice(line, *res.level));
  REQUIRE(is_zero_copy_slice(line, *res.trace_id));
}

TEST_CASE("extract_jsonl_fields supports custom keys, scalars, nulls, and duplicates", "[jsonl]") {
  SECTION("custom keys and empty object") {
    const auto empty = logloom::extract_jsonl_fields("  { } ");
    REQUIRE(empty.kind == logloom::LineKind::JsonObject);
    REQUIRE_FALSE(empty.timestamp.has_value());
    REQUIRE_FALSE(empty.level.has_value());
    REQUIRE_FALSE(empty.trace_id.has_value());

    const std::string line = R"({"ts":1732542000.5,"sev":"WARN","traceId":"tr-42"})";
    const auto res = logloom::extract_jsonl_fields(line, {
        .trace_key = "traceId",
        .timestamp_key = "ts",
        .level_key = "sev",
    });
    REQUIRE(res.kind == logloom::LineKind::JsonObject);
    REQUIRE(res.timestamp == "1732542000.5");
    REQUIRE(res.level == "WARN");
    REQUIRE(res.trace_id == "tr-42");
  }

  SECTION("numeric, boolean, null, and duplicate keys") {
    const std::string line =
        R"({"request_id":12345,"level":true,"timestamp":"t1","request_id":"final-id","level":null,"timestamp":null})";
    const auto res = logloom::extract_jsonl_fields(line);
    REQUIRE(res.kind == logloom::LineKind::JsonObject);
    REQUIRE(res.trace_id == "final-id");
    REQUIRE_FALSE(res.level.has_value());
    REQUIRE_FALSE(res.timestamp.has_value());

    const auto num_trace = logloom::extract_jsonl_fields(R"({"request_id":-42e+2})");
    REQUIRE(num_trace.kind == logloom::LineKind::JsonObject);
    REQUIRE(num_trace.trace_id == "-42e+2");
    REQUIRE_FALSE(num_trace.has_escapes_in_trace_id);
  }
}

TEST_CASE("extract_jsonl_fields handles escapes and ignores fake keys in strings", "[jsonl][escapes]") {
  SECTION("escaped quotes and backslashes inside preceding string do not fool scanner") {
    const std::string line =
        R"({"msg":"escaped \"request_id\":\"fake\" and \\\"level\\\":\"FAKE\"","request_id":"real-1","level":"INFO"})";
    const auto res = logloom::extract_jsonl_fields(line);
    REQUIRE(res.kind == logloom::LineKind::JsonObject);
    REQUIRE(res.trace_id == "real-1");
    REQUIRE(res.level == "INFO");
    REQUIRE_FALSE(res.has_escapes_in_trace_id);
  }

  SECTION("escapes inside trace_id set has_escapes_in_trace_id") {
    const std::string line = R"({"request_id":"req-\"a\"\\\\b\u0020c"})";
    const auto res = logloom::extract_jsonl_fields(line);
    REQUIRE(res.kind == logloom::LineKind::JsonObject);
    REQUIRE(res.trace_id == R"(req-\"a\"\\\\b\u0020c)");
    REQUIRE(res.has_escapes_in_trace_id);
  }

  SECTION("escaped backslash right before closing quote") {
    const std::string line = R"({"request_id":"abc\\","level":"DEBUG"})";
    const auto res = logloom::extract_jsonl_fields(line);
    REQUIRE(res.kind == logloom::LineKind::JsonObject);
    REQUIRE(res.trace_id == R"(abc\\)");
    REQUIRE(res.has_escapes_in_trace_id);
    REQUIRE(res.level == "DEBUG");
  }
}

TEST_CASE("extract_jsonl_fields never matches keys inside nested objects or arrays", "[jsonl][nesting]") {
  const std::string nested_before_and_after =
      R"({"nested":{"request_id":"wrong-1","timestamp":"wrong-ts","level":"FATAL"},)"
      R"("arr":[1,{"request_id":"wrong-2"},[true,{"level":"ERROR"}]],)"
      R"("request_id":{"sub":"not-scalar"},)"
      R"("level":"WARN",)"
      R"("timestamp":"2026-11-25T12:01:00Z",)"
      R"("tail":{"request_id":"wrong-3"}})";

  const auto res = logloom::extract_jsonl_fields(nested_before_and_after);
  REQUIRE(res.kind == logloom::LineKind::JsonObject);
  REQUIRE_FALSE(res.trace_id.has_value());
  REQUIRE(res.level == "WARN");
  REQUIRE(res.timestamp == "2026-11-25T12:01:00Z");

  const std::string nested_then_real =
      R"({"payload":{"request_id":"inner"},"arr":[{"request_id":"inner2"}],"request_id":"outer"})";
  const auto res2 = logloom::extract_jsonl_fields(nested_then_real);
  REQUIRE(res2.kind == logloom::LineKind::JsonObject);
  REQUIRE(res2.trace_id == "outer");
}

TEST_CASE("extract_jsonl_fields classifies plain-text and non-object JSON as PlainText", "[jsonl][plain]") {
  for (const std::string_view line : {
           std::string_view(""),
           std::string_view("   \t  "),
           std::string_view("2026-11-25 ERROR plain log message request_id=abc"),
           std::string_view("[1, 2, {\"request_id\": \"x\"}]"),
           std::string_view("\"just a string\""),
           std::string_view("12345"),
       }) {
    CAPTURE(line);
    const auto res = logloom::extract_jsonl_fields(line);
    REQUIRE(res.kind == logloom::LineKind::PlainText);
    REQUIRE_FALSE(res.timestamp.has_value());
    REQUIRE_FALSE(res.level.has_value());
    REQUIRE_FALSE(res.trace_id.has_value());
  }
}

TEST_CASE("extract_jsonl_fields rejects malformed and hostile JSON safely", "[jsonl][adversarial]") {
  const std::string deep_arrays = "{\"a\":" + std::string(1000, '[') + "1" + std::string(1000, ']') + "}";
  std::string deep_objects = "{";
  for (int i = 0; i < 600; ++i) {
    deep_objects += "\"k\":{";
  }
  deep_objects += "\"x\":1";
  for (int i = 0; i < 600; ++i) {
    deep_objects += "}";
  }
  deep_objects += "}";

  const std::string control_char = std::string("{\"request_id\":\"a\nb\"}");
  const std::string trailing_backslash = R"({"request_id":"abc\)";

  for (const std::string_view bad : {
           std::string_view("{"),
           std::string_view("{\"request_id\":\"req-1\""),
           std::string_view("{\"request_id\":\"req-1\",}"),
           std::string_view("{\"request_id\":\"req-1\"} trailing"),
           std::string_view("{\"request_id\" \"req-1\"}"),
           std::string_view("{request_id:\"req-1\"}"),
           std::string_view(R"({"request_id":"bad\q"})"),
           std::string_view(R"({"request_id":"bad\u123"})"),
           std::string_view(R"({"a":[1,2]})"),
           std::string_view(R"({"a":[1}})"),
           std::string_view(R"({"a":[1 2]})"),
           std::string_view(R"({"a":[1,]})"),
           std::string_view(R"({"a":{"b":1,}})"),
           std::string_view(R"({"a":01})"),
           std::string_view(R"({"a":1.})"),
           std::string_view(R"({"a":1e})"),
           std::string_view(R"({"a":tru})"),
           std::string_view(control_char),
           std::string_view(trailing_backslash),
           std::string_view(deep_arrays),
           std::string_view(deep_objects),
       }) {
    if (bad == std::string_view(R"({"a":[1,2]})")) {
      // Sanity check: valid nested array passes!
      REQUIRE(logloom::extract_jsonl_fields(bad).kind == logloom::LineKind::JsonObject);
      continue;
    }
    CAPTURE(bad);
    const auto res = logloom::extract_jsonl_fields(bad);
    REQUIRE(res.kind == logloom::LineKind::Malformed);
    REQUIRE_FALSE(res.timestamp.has_value());
    REQUIRE_FALSE(res.level.has_value());
    REQUIRE_FALSE(res.trace_id.has_value());
  }
}

TEST_CASE("extract_jsonl_fields survives invalid UTF-8 bytes inside JSON strings", "[jsonl][hostile]") {
  const std::string line =
      std::string("{\"msg\":\"\xFF\xFE\x80\xC0\xAF\",\"request_id\":\"req-utf8\",\"level\":\"INFO\"}");
  const auto res = logloom::extract_jsonl_fields(line);
  REQUIRE(res.kind == logloom::LineKind::JsonObject);
  REQUIRE(res.trace_id == "req-utf8");
  REQUIRE(res.level == "INFO");
}
