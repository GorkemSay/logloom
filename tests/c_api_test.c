#include "logloom/c_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_CHECK(expr)                                                  \
  do {                                                                    \
    if (!(expr)) {                                                        \
      fprintf(stderr, "Assertion failed (%s:%d): %s\n", __FILE__, __LINE__, #expr); \
      abort();                                                            \
    }                                                                     \
  } while (0)

typedef struct progress_state {
  size_t calls;
  uint64_t last_bytes;
  uint64_t total_bytes;
  uint64_t last_lines;
  bool cancel_immediately;
} progress_state_t;

static bool on_progress(
    uint64_t bytes_processed,
    uint64_t total_bytes,
    uint64_t lines_indexed,
    void* user_data) {
  progress_state_t* st = (progress_state_t*)user_data;
  st->calls += 1;
  TEST_CHECK(bytes_processed >= st->last_bytes);
  TEST_CHECK(bytes_processed <= total_bytes);
  TEST_CHECK(lines_indexed >= st->last_lines);
  st->last_bytes = bytes_processed;
  st->total_bytes = total_bytes;
  st->last_lines = lines_indexed;
  return !st->cancel_immediately;
}

typedef struct match_collector {
  size_t count;
  uint64_t first_line;
  uint64_t last_line;
  size_t stop_after;
} match_collector_t;

static bool on_match(const logloom_search_match_t* match, void* user_data) {
  match_collector_t* col = (match_collector_t*)user_data;
  TEST_CHECK(match != NULL);
  TEST_CHECK(match->content != NULL);
  TEST_CHECK(strlen(match->content) == match->content_length);

  if (col->count == 0) {
    col->first_line = match->line_number;
  }
  col->last_line = match->line_number;
  col->count += 1;

  if (col->stop_after > 0 && col->count >= col->stop_after) {
    return false;
  }
  return true;
}

static void write_fixture(const char* path, const char* data) {
  FILE* fp = fopen(path, "wb");
  TEST_CHECK(fp != NULL);
  const size_t len = strlen(data);
  const size_t written = fwrite(data, 1, len, fp);
  TEST_CHECK(written == len);
  TEST_CHECK(fclose(fp) == 0);
}

int main(void) {
  /* 1. Version & error basics */
  TEST_CHECK(strcmp(logloom_version(), "0.1.0") == 0);
  TEST_CHECK(logloom_last_error() != NULL);

  logloom_file_t* bad_file = NULL;
  TEST_CHECK(logloom_file_open(NULL, &bad_file) == LOGLOOM_ERR_INVALID_ARG);
  TEST_CHECK(bad_file == NULL);
  TEST_CHECK(strlen(logloom_last_error()) > 0);

  TEST_CHECK(logloom_file_open("/tmp/logloom_c_missing_999.jsonl", &bad_file) == LOGLOOM_ERR_IO);
  TEST_CHECK(bad_file == NULL);
  TEST_CHECK(strlen(logloom_last_error()) > 0);

  /* 2. Prepare temp fixture */
  const char* path = "/tmp/logloom_c_api_test_fixture.jsonl";
  const char* payload =
      "{\"timestamp\":\"2026-11-26T10:00:00Z\",\"level\":\"INFO\",\"request_id\":\"req-1\",\"msg\":\"boot\"}\n"
      "plain noise line\n"
      "{\"timestamp\":\"2026-11-26T10:00:01Z\",\"level\":\"ERROR\",\"request_id\":\"req-2\",\"msg\":\"timeout\"}\r\n"
      "{\"timestamp\":\"2026-11-26T10:00:02Z\",\"level\":\"WARN\",\"request_id\":\"req-1\",\"msg\":\"retry\"}\n"
      "{malformed\n"
      "{\"timestamp\":\"2026-11-26T10:00:03Z\",\"level\":\"INFO\",\"request_id\":\"req-1\",\"msg\":\"eof";
  write_fixture(path, payload);

  logloom_file_t* file = NULL;
  TEST_CHECK(logloom_file_open(path, &file) == LOGLOOM_OK);
  TEST_CHECK(file != NULL);
  TEST_CHECK(logloom_file_size(file) == (uint64_t)strlen(payload));
  TEST_CHECK(!logloom_file_is_indexed(file));
  TEST_CHECK(logloom_file_line_count(file) == 0);

  /* 3. Read before indexing must fail cleanly with LOGLOOM_ERR_NOT_INDEXED */
  logloom_line_t line = {0};
  TEST_CHECK(logloom_file_read_line(file, 1, &line) == LOGLOOM_ERR_NOT_INDEXED);
  TEST_CHECK(strlen(logloom_file_last_error(file)) > 0);
  logloom_line_free(&line);

  /* 4. Cancel token + cancelled index build */
  logloom_cancel_token_t* token = logloom_cancel_token_create();
  TEST_CHECK(token != NULL);
  TEST_CHECK(!logloom_cancel_token_is_cancelled(token));
  logloom_cancel_token_cancel(token);
  TEST_CHECK(logloom_cancel_token_is_cancelled(token));

  logloom_index_stats_t idx_stats = {0};
  TEST_CHECK(logloom_file_build_index(file, NULL, NULL, token, &idx_stats) == LOGLOOM_ERR_CANCELLED);
  TEST_CHECK(!logloom_file_is_indexed(file));

  logloom_cancel_token_reset(token);
  TEST_CHECK(!logloom_cancel_token_is_cancelled(token));

  /* Progress callback returning false cancels indexing */
  progress_state_t cancel_prog = {.cancel_immediately = true};
  TEST_CHECK(logloom_file_build_index(file, on_progress, &cancel_prog, NULL, &idx_stats) ==
             LOGLOOM_ERR_CANCELLED);
  TEST_CHECK(!logloom_file_is_indexed(file));

  /* 5. Full index build with progress callback */
  progress_state_t prog = {0};
  TEST_CHECK(logloom_file_build_index(file, on_progress, &prog, token, &idx_stats) == LOGLOOM_OK);
  TEST_CHECK(logloom_file_is_indexed(file));
  TEST_CHECK(logloom_file_line_count(file) == 6);
  TEST_CHECK(idx_stats.total_lines == 6);
  TEST_CHECK(idx_stats.has_truncated_final_line);
  TEST_CHECK(!idx_stats.had_utf8_bom);
  TEST_CHECK(prog.calls >= 1);
  TEST_CHECK(prog.last_bytes == prog.total_bytes);
  TEST_CHECK(prog.last_lines == 6);

  /* 6. O(1) read_line & read_context */
  TEST_CHECK(logloom_file_read_line(file, 3, &line) == LOGLOOM_OK);
  TEST_CHECK(line.line_number == 3);
  TEST_CHECK(line.has_crlf);
  TEST_CHECK(!line.is_truncated_eof);
  TEST_CHECK(strstr(line.content, "req-2") != NULL);
  logloom_line_free(&line);

  TEST_CHECK(logloom_file_read_line(file, 6, &line) == LOGLOOM_OK);
  TEST_CHECK(line.line_number == 6);
  TEST_CHECK(line.is_truncated_eof);
  logloom_line_free(&line);

  TEST_CHECK(logloom_file_read_line(file, 0, &line) == LOGLOOM_ERR_OUT_OF_RANGE);
  TEST_CHECK(logloom_file_read_line(file, 7, &line) == LOGLOOM_ERR_OUT_OF_RANGE);

  logloom_line_list_t ctx = {0};
  TEST_CHECK(logloom_file_read_context(file, 3, 1, 1, &ctx) == LOGLOOM_OK);
  TEST_CHECK(ctx.count == 3);
  TEST_CHECK(ctx.lines[0].line_number == 2);
  TEST_CHECK(ctx.lines[1].line_number == 3);
  TEST_CHECK(ctx.lines[2].line_number == 4);
  logloom_line_list_free(&ctx);

  /* 7. Streaming search: trace isolation, level + regex, invalid regex, cancellation */
  logloom_search_query_t q = logloom_search_query_default();
  q.trace_id = "req-1";
  match_collector_t trace_col = {0};
  logloom_search_stats_t s_stats = {0};
  TEST_CHECK(logloom_file_search(file, &q, on_match, &trace_col, token, &s_stats) == LOGLOOM_OK);
  TEST_CHECK(trace_col.count == 2);
  TEST_CHECK(trace_col.first_line == 1);
  TEST_CHECK(trace_col.last_line == 4);
  TEST_CHECK(s_stats.lines_scanned == 6);
  TEST_CHECK(s_stats.matches_found == 2);
  TEST_CHECK(s_stats.json_lines == 3);
  TEST_CHECK(s_stats.plain_text_lines == 1);
  TEST_CHECK(s_stats.malformed_lines == 2);
  TEST_CHECK(s_stats.has_truncated_final_line);

  logloom_search_query_t rx_q = logloom_search_query_default();
  rx_q.text_query = "timeout|retry";
  rx_q.is_regex = true;
  rx_q.case_sensitive = false;
  match_collector_t rx_col = {0};
  TEST_CHECK(logloom_file_search(file, &rx_q, on_match, &rx_col, NULL, &s_stats) == LOGLOOM_OK);
  TEST_CHECK(rx_col.count == 2);

  logloom_search_query_t bad_rx = logloom_search_query_default();
  bad_rx.text_query = "[unclosed";
  bad_rx.is_regex = true;
  TEST_CHECK(logloom_file_search(file, &bad_rx, NULL, NULL, NULL, &s_stats) ==
             LOGLOOM_ERR_INVALID_REGEX);

  /* Callback early stop */
  logloom_search_query_t all_q = logloom_search_query_default();
  match_collector_t stop_col = {.stop_after = 2};
  TEST_CHECK(logloom_file_search(file, &all_q, on_match, &stop_col, NULL, &s_stats) ==
             LOGLOOM_ERR_CANCELLED);
  TEST_CHECK(stop_col.count == 2);
  TEST_CHECK(s_stats.cancelled);

  /* Cancel token pre-cancelled */
  logloom_cancel_token_cancel(token);
  TEST_CHECK(logloom_file_search(file, &all_q, NULL, NULL, token, &s_stats) ==
             LOGLOOM_ERR_CANCELLED);
  TEST_CHECK(s_stats.cancelled);

  /* 8. Clean up */
  logloom_cancel_token_destroy(token);
  logloom_file_close(file);
  logloom_file_close(NULL);
  logloom_cancel_token_destroy(NULL);
  logloom_line_free(NULL);
  logloom_line_list_free(NULL);
  remove(path);

  printf("logloom_c_api_test: all assertions passed\n");
  return 0;
}
