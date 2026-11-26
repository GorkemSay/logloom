#ifndef LOGLOOM_C_API_H
#define LOGLOOM_C_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum logloom_status {
  LOGLOOM_OK = 0,
  LOGLOOM_ERR_INVALID_ARG = 1,
  LOGLOOM_ERR_IO = 2,
  LOGLOOM_ERR_OUT_OF_RANGE = 3,
  LOGLOOM_ERR_NOT_INDEXED = 4,
  LOGLOOM_ERR_INVALID_REGEX = 5,
  LOGLOOM_ERR_CANCELLED = 6,
  LOGLOOM_ERR_INTERNAL = 7
} logloom_status_t;

typedef enum logloom_line_kind {
  LOGLOOM_LINE_PLAIN_TEXT = 0,
  LOGLOOM_LINE_JSON_OBJECT = 1,
  LOGLOOM_LINE_MALFORMED = 2
} logloom_line_kind_t;

typedef struct logloom_file logloom_file_t;
typedef struct logloom_cancel_token logloom_cancel_token_t;

typedef struct logloom_index_stats {
  uint64_t total_lines;
  uint64_t oversized_lines;
  uint64_t bytes_read;
  uint64_t stride;
  size_t index_memory_bytes;
  bool had_utf8_bom;
  bool has_truncated_final_line;
} logloom_index_stats_t;

typedef struct logloom_line {
  uint64_t line_number;
  uint64_t byte_offset;
  uint64_t byte_length;
  char* content;
  size_t content_length;
  bool has_crlf;
  bool is_truncated_eof;
  bool exceeds_max_length;
} logloom_line_t;

typedef struct logloom_line_list {
  logloom_line_t* lines;
  size_t count;
} logloom_line_list_t;

typedef struct logloom_extracted_fields {
  logloom_line_kind_t kind;
  const char* timestamp;
  size_t timestamp_length;
  const char* level;
  size_t level_length;
  const char* trace_id;
  size_t trace_id_length;
  bool has_escapes_in_trace_id;
} logloom_extracted_fields_t;

typedef struct logloom_search_query {
  const char* trace_id;
  const char* trace_key;
  const char* level;
  const char* level_key;
  const char* timestamp_key;
  const char* text_query;
  bool case_sensitive;
  bool is_regex;
  uint64_t limit;
  size_t num_threads;
} logloom_search_query_t;

typedef struct logloom_search_match {
  uint64_t line_number;
  uint64_t byte_offset;
  uint64_t byte_length;
  const char* content;
  size_t content_length;
  logloom_line_kind_t kind;
  const char* timestamp;
  size_t timestamp_length;
  const char* level;
  size_t level_length;
  const char* trace_id;
  size_t trace_id_length;
  bool has_crlf;
  bool is_truncated_eof;
} logloom_search_match_t;

typedef struct logloom_search_stats {
  uint64_t lines_scanned;
  uint64_t matches_found;
  uint64_t json_lines;
  uint64_t plain_text_lines;
  uint64_t malformed_lines;
  uint64_t oversized_lines;
  uint64_t truncated_lines;
  bool has_truncated_final_line;
  bool cancelled;
  bool limit_reached;
} logloom_search_stats_t;

typedef bool (*logloom_progress_cb)(
    uint64_t bytes_processed,
    uint64_t total_bytes,
    uint64_t lines_indexed,
    void* user_data);

typedef bool (*logloom_match_cb)(
    const logloom_search_match_t* match,
    void* user_data);

/* Returns the semantic version string of liblogloom. */
const char* logloom_version(void);

/* Returns the calling thread's most recent error message (never NULL). */
const char* logloom_last_error(void);

/* Returns the most recent error message recorded on `file` (never NULL). */
const char* logloom_file_last_error(const logloom_file_t* file);

/* Cancel token lifecycle. */
logloom_cancel_token_t* logloom_cancel_token_create(void);
void logloom_cancel_token_destroy(logloom_cancel_token_t* token);
void logloom_cancel_token_cancel(logloom_cancel_token_t* token);
void logloom_cancel_token_reset(logloom_cancel_token_t* token);
bool logloom_cancel_token_is_cancelled(const logloom_cancel_token_t* token);

/* Read-only file handle lifecycle. */
logloom_status_t logloom_file_open(const char* path, logloom_file_t** out_file);
void logloom_file_close(logloom_file_t* file);
uint64_t logloom_file_size(const logloom_file_t* file);

/* Builds (or rebuilds) the adaptive byte-offset LineIndex for `file`. */
logloom_status_t logloom_file_build_index(
    logloom_file_t* file,
    logloom_progress_cb progress_cb,
    void* user_data,
    const logloom_cancel_token_t* cancel_token,
    logloom_index_stats_t* out_stats);

bool logloom_file_is_indexed(const logloom_file_t* file);
uint64_t logloom_file_line_count(const logloom_file_t* file);

/* O(1) indexed line and context retrieval. Caller frees via *_free helpers. */
logloom_status_t logloom_file_read_line(
    const logloom_file_t* file,
    uint64_t line_number,
    logloom_line_t* out_line);

void logloom_line_free(logloom_line_t* line);

logloom_status_t logloom_file_read_context(
    const logloom_file_t* file,
    uint64_t center_line,
    uint64_t before,
    uint64_t after,
    logloom_line_list_t* out_list);

/* Reads up to `max_lines` from the start of `file` without requiring an index.
   Useful for instant first-screen rendering while background indexing runs. */
logloom_status_t logloom_file_read_head(
    const logloom_file_t* file,
    size_t max_lines,
    logloom_line_list_t* out_list);

void logloom_line_list_free(logloom_line_list_t* list);

/* Zero-copy JSONL top-level field extraction over `line[0..line_length)`.
   Returned pointers alias `line` (not NUL-terminated; use *_length).
   Pass NULL for any key parameter to use its default ("request_id", "timestamp", "level"). */
logloom_status_t logloom_extract_fields(
    const char* line,
    size_t line_length,
    const char* trace_key,
    const char* timestamp_key,
    const char* level_key,
    logloom_extracted_fields_t* out_fields);

/* Streaming parallel search and trace isolation. */
logloom_search_query_t logloom_search_query_default(void);

logloom_status_t logloom_file_search(
    const logloom_file_t* file,
    const logloom_search_query_t* query,
    logloom_match_cb match_cb,
    void* user_data,
    const logloom_cancel_token_t* cancel_token,
    logloom_search_stats_t* out_stats);

#ifdef __cplusplus
}
#endif

#endif /* LOGLOOM_C_API_H */
