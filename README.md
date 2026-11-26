# Logloom

High-performance, local-first log and JSONL trace inspector built on a C++20 core engine (`liblogloom`) with a CLI and a planned native macOS desktop app.

## Architecture

- **`core/` (`liblogloom`):** UI-agnostic C++20 static library for safe POSIX I/O, indexing, JSONL field extraction, and search.
- **`cli/` (`logloom`):** Command-line interface consuming `liblogloom`.
- **`tests/`:** Catch2 v3 unit, edge-case, and integration tests verified under AddressSanitizer and UndefinedBehaviorSanitizer.
- **`macos/` *(Phase 2)*:** Native SwiftUI + AppKit desktop application linking `liblogloom` via a C ABI (`include/logloom/c_api.h`).

## Prerequisites

- C++20 compiler (Clang, Apple Clang, or GCC)
- CMake >= 3.24
- Ninja

## Build & Test

### Debug with ASan & UBSan (`debug-asan`)

```bash
cmake --preset debug-asan
cmake --build --preset debug-asan
ctest --preset debug-asan --output-on-failure
```

### Release (`release`)

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
```

### CLI Quick Check

```bash
./build/release/cli/logloom --version
```

## CLI Usage

`logloom` writes matched/requested data to `stdout` and diagnostics/warnings/errors to `stderr`. ANSI styling is disabled automatically when piped or when `NO_COLOR` is set.

```bash
# Isolate all lines for a request_id (with optional context & JSONL export)
./build/release/cli/logloom trace app.jsonl --id req-9941 --context 3 --export trace.jsonl

# Isolate using a custom JSON trace key
./build/release/cli/logloom trace app.jsonl --id tr-42 --key trace_id

# Search by substring/regex and optional JSONL level filter
./build/release/cli/logloom search app.jsonl --query "timeout|deadlock" --regex -i --level ERROR --limit 50

# O(1) seek around a specific 1-based line number
./build/release/cli/logloom context app.jsonl --line 12500 --before 10 --after 10

# Inspect file size, line counts, and malformed/truncated-EOF diagnostics
./build/release/cli/logloom stats app.jsonl
```

**Exit Codes:**
- `0`: Matches found / command succeeded
- `1`: No matches found (or requested line out of range)
- `2`: Usage, regex, or I/O error

## Verified Benchmarks

Measured on **Apple M4** (10 cores, 16 GiB RAM, macOS 27.2.1, `release` preset) using deterministic seeded JSONL datasets from `tools/logloom-gen`. Full methodology, 100 MiB / 1 GiB / 5 GiB tables, and `pread` vs. `mmap` analysis are in [BENCHMARKS.md](./BENCHMARKS.md).

| Dataset | Mode | Duration | Throughput | Peak RSS |
| --- | --- | ---: | ---: | ---: |
| 1 GiB (`5.50M` lines) | `pread-parallel` | `141–151 ms` | `6,779–7,262 MiB/s` | `5.94–6.44 MiB` |
| 1 GiB (`5.50M` lines) | `mmap-parallel` | `136 ms` | `7,510–7,525 MiB/s` | `1,026 MiB` |
| 1 GiB (`5.50M` lines) | `jq -R fromjson?` | `12,450 ms` | `82.25 MiB/s` | `2.36 MiB` |
| 5 GiB (`27.51M` lines) | `pread-parallel` | `1,856–1,942 ms` | `2,636–2,758 MiB/s` | `5.61–6.95 MiB` |
| 5 GiB (`27.51M` lines) | `mmap-parallel` | `5,611–6,153 ms` | `832–913 MiB/s` | `3,895–3,914 MiB` |
| 5 GiB (`27.51M` lines) | `LineIndex::build` | `1,600–1,794 ms` | `2,854–3,201 MiB/s` | `31.69–33.30 MiB` (`13.12 MiB` index) |

## C ABI (`liblogloom`)

`include/logloom/c_api.h` provides an opaque-handle, C11-compatible ABI over `liblogloom.a` for native integrations (such as the planned Phase 2 Swift/macOS desktop app):

- File lifecycle: `logloom_file_open`, `logloom_file_close`, `logloom_file_size`
- Incremental indexing with progress & cancellation: `logloom_file_build_index`, `logloom_cancel_token_*`
- O(1) indexed line & context fetch: `logloom_file_read_line`, `logloom_file_read_context`
- Streaming parallel search & trace isolation: `logloom_file_search`
- Thread-safe error retrieval: `logloom_last_error`, `logloom_file_last_error`

## Project Documentation

- [SPEC.md](./SPEC.md) — Engineering specification and step-by-step roadmap
- [PROGRESS.md](./PROGRESS.md) — Living development journal and status log
- [BENCHMARKS.md](./BENCHMARKS.md) — Reproducible benchmark methodology and results
