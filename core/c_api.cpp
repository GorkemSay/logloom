#include "logloom/c_api.h"

#include "logloom/line_index.hpp"
#include "logloom/posix_file.hpp"
#include "logloom/search_engine.hpp"
#include "logloom/version.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

struct logloom_cancel_token {
  std::atomic<bool> cancelled{false};
};

struct logloom_file {
  logloom::PosixFile handle{};
  mutable std::shared_mutex index_mutex{};
  std::optional<logloom::LineIndex> index{};
  mutable std::mutex error_mutex{};
  std::string last_error{};
};

namespace {

thread_local std::string tls_last_error;

void set_error(const logloom_file_t* file, const std::string& msg) noexcept {
  try {
    tls_last_error = msg;
    if (file != nullptr) {
      std::lock_guard<std::mutex> lock(file->error_mutex);
      const_cast<logloom_file_t*>(file)->last_error = msg;
    }
  } catch (...) {
  }
}

void clear_error(const logloom_file_t* file) noexcept {
  try {
    tls_last_error.clear();
    if (file != nullptr) {
      std::lock_guard<std::mutex> lock(file->error_mutex);
      const_cast<logloom_file_t*>(file)->last_error.clear();
    }
  } catch (...) {
  }
}

char* dup_string(std::string_view sv) noexcept {
  char* buf = static_cast<char*>(std::malloc(sv.size() + 1));
  if (buf == nullptr) {
    return nullptr;
  }
  if (!sv.empty()) {
    std::memcpy(buf, sv.data(), sv.size());
  }
  buf[sv.size()] = '\0';
  return buf;
}

bool fill_c_line(const logloom::IndexedLine& src, logloom_line_t& dst) noexcept {
  dst.line_number = src.line_number;
  dst.byte_offset = src.byte_offset;
  dst.byte_length = src.byte_length;
  dst.content = dup_string(src.content);
  if (dst.content == nullptr) {
    dst.content_length = 0;
    return false;
  }
  dst.content_length = src.content.size();
  dst.has_crlf = src.has_crlf;
  dst.is_truncated_eof = src.is_truncated_eof;
  dst.exceeds_max_length = src.exceeds_max_length;
  return true;
}

logloom_line_kind_t to_c_kind(logloom::LineKind kind) noexcept {
  switch (kind) {
    case logloom::LineKind::JsonObject:
      return LOGLOOM_LINE_JSON_OBJECT;
    case logloom::LineKind::Malformed:
      return LOGLOOM_LINE_MALFORMED;
    case logloom::LineKind::PlainText:
    default:
      return LOGLOOM_LINE_PLAIN_TEXT;
  }
}

} // namespace

extern "C" {

const char* logloom_version(void) {
  static const std::string ver(logloom::version());
  return ver.c_str();
}

const char* logloom_last_error(void) {
  return tls_last_error.c_str();
}

const char* logloom_file_last_error(const logloom_file_t* file) {
  if (file == nullptr) {
    return tls_last_error.c_str();
  }
  try {
    std::lock_guard<std::mutex> lock(file->error_mutex);
    tls_last_error = file->last_error;
  } catch (...) {
    return "";
  }
  return tls_last_error.c_str();
}

logloom_cancel_token_t* logloom_cancel_token_create(void) {
  try {
    return new logloom_cancel_token_t{};
  } catch (...) {
    set_error(nullptr, "out of memory creating cancel token");
    return nullptr;
  }
}

void logloom_cancel_token_destroy(logloom_cancel_token_t* token) {
  delete token;
}

void logloom_cancel_token_cancel(logloom_cancel_token_t* token) {
  if (token != nullptr) {
    token->cancelled.store(true, std::memory_order_release);
  }
}

void logloom_cancel_token_reset(logloom_cancel_token_t* token) {
  if (token != nullptr) {
    token->cancelled.store(false, std::memory_order_release);
  }
}

bool logloom_cancel_token_is_cancelled(const logloom_cancel_token_t* token) {
  if (token == nullptr) {
    return false;
  }
  return token->cancelled.load(std::memory_order_acquire);
}

logloom_status_t logloom_file_open(const char* path, logloom_file_t** out_file) {
  if (out_file != nullptr) {
    *out_file = nullptr;
  }
  if (path == nullptr || path[0] == '\0' || out_file == nullptr) {
    set_error(nullptr, "invalid arguments to logloom_file_open");
    return LOGLOOM_ERR_INVALID_ARG;
  }

  try {
    std::error_code ec;
    auto posix = logloom::PosixFile::open_read_only(path, ec);
    if (!posix.has_value()) {
      set_error(nullptr, ec.message());
      return LOGLOOM_ERR_IO;
    }

    auto wrapper = std::make_unique<logloom_file_t>();
    wrapper->handle = std::move(*posix);
    *out_file = wrapper.release();
    clear_error(*out_file);
    return LOGLOOM_OK;
  } catch (const std::exception& ex) {
    set_error(nullptr, ex.what());
    return LOGLOOM_ERR_INTERNAL;
  } catch (...) {
    set_error(nullptr, "unknown internal error in logloom_file_open");
    return LOGLOOM_ERR_INTERNAL;
  }
}

void logloom_file_close(logloom_file_t* file) {
  delete file;
}

uint64_t logloom_file_size(const logloom_file_t* file) {
  if (file == nullptr || !file->handle.is_open()) {
    return 0;
  }
  return file->handle.size();
}

logloom_status_t logloom_file_build_index(
    logloom_file_t* file,
    logloom_progress_cb progress_cb,
    void* user_data,
    const logloom_cancel_token_t* cancel_token,
    logloom_index_stats_t* out_stats) {
  if (file == nullptr || !file->handle.is_open()) {
    set_error(file, "invalid file handle");
    return LOGLOOM_ERR_INVALID_ARG;
  }

  try {
    clear_error(file);
    std::error_code ec;
    logloom::IndexProgressCallback cpp_progress;
    if (progress_cb != nullptr) {
      cpp_progress = [progress_cb, user_data](
                         std::uint64_t bytes_processed,
                         std::uint64_t total_bytes,
                         std::uint64_t lines_indexed) {
        return progress_cb(bytes_processed, total_bytes, lines_indexed, user_data);
      };
    }

    const std::atomic<bool>* cancel_ptr =
        (cancel_token != nullptr) ? &cancel_token->cancelled : nullptr;

    auto built = logloom::LineIndex::build(
        file->handle, ec, logloom::LineIndexOptions{}, cpp_progress, cancel_ptr);
    if (!built.has_value()) {
      if (ec == std::errc::operation_canceled) {
        set_error(file, "indexing cancelled");
        return LOGLOOM_ERR_CANCELLED;
      }
      set_error(file, ec.message());
      return LOGLOOM_ERR_IO;
    }

    if (out_stats != nullptr) {
      const auto& st = built->stats();
      out_stats->total_lines = st.total_lines;
      out_stats->oversized_lines = st.oversized_lines;
      out_stats->bytes_read = st.bytes_read;
      out_stats->stride = built->stride();
      out_stats->index_memory_bytes = built->memory_bytes();
      out_stats->had_utf8_bom = st.had_utf8_bom;
      out_stats->has_truncated_final_line = st.has_truncated_final_line;
    }

    std::unique_lock<std::shared_mutex> lock(file->index_mutex);
    file->index = std::move(*built);
    return LOGLOOM_OK;
  } catch (const std::exception& ex) {
    set_error(file, ex.what());
    return LOGLOOM_ERR_INTERNAL;
  } catch (...) {
    set_error(file, "unknown internal error in logloom_file_build_index");
    return LOGLOOM_ERR_INTERNAL;
  }
}

bool logloom_file_is_indexed(const logloom_file_t* file) {
  if (file == nullptr) {
    return false;
  }
  std::shared_lock<std::shared_mutex> lock(file->index_mutex);
  return file->index.has_value();
}

uint64_t logloom_file_line_count(const logloom_file_t* file) {
  if (file == nullptr) {
    return 0;
  }
  std::shared_lock<std::shared_mutex> lock(file->index_mutex);
  if (!file->index.has_value()) {
    return 0;
  }
  return file->index->line_count();
}

logloom_status_t logloom_file_read_line(
    const logloom_file_t* file,
    uint64_t line_number,
    logloom_line_t* out_line) {
  if (out_line != nullptr) {
    std::memset(out_line, 0, sizeof(*out_line));
  }
  if (file == nullptr || !file->handle.is_open() || out_line == nullptr) {
    set_error(file, "invalid arguments to logloom_file_read_line");
    return LOGLOOM_ERR_INVALID_ARG;
  }

  try {
    clear_error(file);
    std::shared_lock<std::shared_mutex> lock(file->index_mutex);
    if (!file->index.has_value()) {
      set_error(file, "file has not been indexed yet");
      return LOGLOOM_ERR_NOT_INDEXED;
    }

    if (line_number == 0 || line_number > file->index->line_count()) {
      set_error(file, "line number out of range");
      return LOGLOOM_ERR_OUT_OF_RANGE;
    }

    logloom::IndexedLine line{};
    std::error_code ec;
    if (!file->index->read_line(file->handle, line_number, line, ec)) {
      if (ec) {
        set_error(file, ec.message());
        return LOGLOOM_ERR_IO;
      }
      set_error(file, "line number out of range");
      return LOGLOOM_ERR_OUT_OF_RANGE;
    }

    if (!fill_c_line(line, *out_line)) {
      logloom_line_free(out_line);
      set_error(file, "out of memory allocating line content");
      return LOGLOOM_ERR_INTERNAL;
    }
    return LOGLOOM_OK;
  } catch (const std::exception& ex) {
    logloom_line_free(out_line);
    set_error(file, ex.what());
    return LOGLOOM_ERR_INTERNAL;
  } catch (...) {
    logloom_line_free(out_line);
    set_error(file, "unknown internal error in logloom_file_read_line");
    return LOGLOOM_ERR_INTERNAL;
  }
}

void logloom_line_free(logloom_line_t* line) {
  if (line == nullptr) {
    return;
  }
  std::free(line->content);
  std::memset(line, 0, sizeof(*line));
}

logloom_status_t logloom_file_read_context(
    const logloom_file_t* file,
    uint64_t center_line,
    uint64_t before,
    uint64_t after,
    logloom_line_list_t* out_list) {
  if (out_list != nullptr) {
    out_list->lines = nullptr;
    out_list->count = 0;
  }
  if (file == nullptr || !file->handle.is_open() || out_list == nullptr) {
    set_error(file, "invalid arguments to logloom_file_read_context");
    return LOGLOOM_ERR_INVALID_ARG;
  }

  try {
    clear_error(file);
    std::shared_lock<std::shared_mutex> lock(file->index_mutex);
    if (!file->index.has_value()) {
      set_error(file, "file has not been indexed yet");
      return LOGLOOM_ERR_NOT_INDEXED;
    }

    if (center_line == 0 || center_line > file->index->line_count()) {
      set_error(file, "center line out of range");
      return LOGLOOM_ERR_OUT_OF_RANGE;
    }

    std::vector<logloom::IndexedLine> ctx;
    std::error_code ec;
    if (!file->index->read_context(file->handle, center_line, before, after, ctx, ec)) {
      if (ec) {
        set_error(file, ec.message());
        return LOGLOOM_ERR_IO;
      }
      set_error(file, "center line out of range");
      return LOGLOOM_ERR_OUT_OF_RANGE;
    }

    if (ctx.empty()) {
      return LOGLOOM_OK;
    }

    auto* arr = static_cast<logloom_line_t*>(
        std::calloc(ctx.size(), sizeof(logloom_line_t)));
    if (arr == nullptr) {
      set_error(file, "out of memory allocating context lines");
      return LOGLOOM_ERR_INTERNAL;
    }
    out_list->lines = arr;
    out_list->count = ctx.size();

    for (std::size_t i = 0; i < ctx.size(); ++i) {
      if (!fill_c_line(ctx[i], arr[i])) {
        logloom_line_list_free(out_list);
        set_error(file, "out of memory allocating context line content");
        return LOGLOOM_ERR_INTERNAL;
      }
    }
    return LOGLOOM_OK;
  } catch (const std::exception& ex) {
    logloom_line_list_free(out_list);
    set_error(file, ex.what());
    return LOGLOOM_ERR_INTERNAL;
  } catch (...) {
    logloom_line_list_free(out_list);
    set_error(file, "unknown internal error in logloom_file_read_context");
    return LOGLOOM_ERR_INTERNAL;
  }
}

void logloom_line_list_free(logloom_line_list_t* list) {
  if (list == nullptr) {
    return;
  }
  if (list->lines != nullptr) {
    for (std::size_t i = 0; i < list->count; ++i) {
      std::free(list->lines[i].content);
    }
    std::free(list->lines);
  }
  list->lines = nullptr;
  list->count = 0;
}

logloom_search_query_t logloom_search_query_default(void) {
  logloom_search_query_t q{};
  q.trace_id = nullptr;
  q.trace_key = "request_id";
  q.level = nullptr;
  q.level_key = "level";
  q.timestamp_key = "timestamp";
  q.text_query = nullptr;
  q.case_sensitive = true;
  q.is_regex = false;
  q.limit = 0;
  q.num_threads = 0;
  return q;
}

logloom_status_t logloom_file_search(
    const logloom_file_t* file,
    const logloom_search_query_t* query,
    logloom_match_cb match_cb,
    void* user_data,
    const logloom_cancel_token_t* cancel_token,
    logloom_search_stats_t* out_stats) {
  if (out_stats != nullptr) {
    std::memset(out_stats, 0, sizeof(*out_stats));
  }
  if (file == nullptr || !file->handle.is_open()) {
    set_error(file, "invalid file handle");
    return LOGLOOM_ERR_INVALID_ARG;
  }

  try {
    clear_error(file);
    logloom::SearchQuery cpp_query;
    std::size_t num_threads = 0;

    if (query != nullptr) {
      if (query->trace_id != nullptr) {
        cpp_query.trace_id = std::string(query->trace_id);
      }
      if (query->trace_key != nullptr && query->trace_key[0] != '\0') {
        cpp_query.trace_key = std::string(query->trace_key);
      }
      if (query->level != nullptr) {
        cpp_query.level = std::string(query->level);
      }
      if (query->level_key != nullptr && query->level_key[0] != '\0') {
        cpp_query.level_key = std::string(query->level_key);
      }
      if (query->timestamp_key != nullptr && query->timestamp_key[0] != '\0') {
        cpp_query.timestamp_key = std::string(query->timestamp_key);
      }
      if (query->text_query != nullptr) {
        cpp_query.text_query = std::string(query->text_query);
      }
      cpp_query.case_sensitive = query->case_sensitive;
      cpp_query.is_regex = query->is_regex;
      cpp_query.limit = query->limit;
      num_threads = query->num_threads;
    }

    const std::atomic<bool>* cancel_ptr =
        (cancel_token != nullptr) ? &cancel_token->cancelled : nullptr;

    std::string content_buf;
    std::string ts_buf;
    std::string level_buf;
    std::string trace_buf;

    logloom::MatchCallback cpp_cb;
    if (match_cb != nullptr) {
      cpp_cb = [&](const logloom::SearchMatch& m) {
        content_buf.assign(m.content);

        logloom_search_match_t cm{};
        cm.line_number = m.line_number;
        cm.byte_offset = m.byte_offset;
        cm.byte_length = m.byte_length;
        cm.content = content_buf.c_str();
        cm.content_length = content_buf.size();
        cm.kind = to_c_kind(m.fields.kind);
        cm.has_crlf = m.has_crlf;
        cm.is_truncated_eof = m.is_truncated_eof;

        if (m.fields.timestamp.has_value()) {
          ts_buf.assign(*m.fields.timestamp);
          cm.timestamp = ts_buf.c_str();
          cm.timestamp_length = ts_buf.size();
        }
        if (m.fields.level.has_value()) {
          level_buf.assign(*m.fields.level);
          cm.level = level_buf.c_str();
          cm.level_length = level_buf.size();
        }
        if (m.fields.trace_id.has_value()) {
          trace_buf.assign(*m.fields.trace_id);
          cm.trace_id = trace_buf.c_str();
          cm.trace_id_length = trace_buf.size();
        }

        return match_cb(&cm, user_data);
      };
    }

    std::error_code ec;
    const auto stats = logloom::search_file_parallel(
        file->handle,
        cpp_query,
        cpp_cb,
        ec,
        logloom::ParallelSearchOptions{.num_threads = num_threads},
        cancel_ptr);

    if (out_stats != nullptr) {
      out_stats->lines_scanned = stats.lines_scanned;
      out_stats->matches_found = stats.matches_found;
      out_stats->json_lines = stats.json_lines;
      out_stats->plain_text_lines = stats.plain_text_lines;
      out_stats->malformed_lines = stats.malformed_lines;
      out_stats->oversized_lines = stats.oversized_lines;
      out_stats->truncated_lines = stats.truncated_lines;
      out_stats->has_truncated_final_line = stats.has_truncated_final_line;
      out_stats->cancelled = stats.cancelled;
      out_stats->limit_reached = stats.limit_reached;
    }

    if (ec) {
      if (ec == std::errc::invalid_argument) {
        set_error(file, "invalid regular expression");
        return LOGLOOM_ERR_INVALID_REGEX;
      }
      set_error(file, ec.message());
      return LOGLOOM_ERR_IO;
    }

    if (stats.cancelled) {
      set_error(file, "search cancelled");
      return LOGLOOM_ERR_CANCELLED;
    }

    return LOGLOOM_OK;
  } catch (const std::exception& ex) {
    set_error(file, ex.what());
    return LOGLOOM_ERR_INTERNAL;
  } catch (...) {
    set_error(file, "unknown internal error in logloom_file_search");
    return LOGLOOM_ERR_INTERNAL;
  }
}

} // extern "C"
