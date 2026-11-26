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

## Project Documentation

- [SPEC.md](./SPEC.md) — Engineering specification and step-by-step roadmap
- [PROGRESS.md](./PROGRESS.md) — Living development journal and status log
