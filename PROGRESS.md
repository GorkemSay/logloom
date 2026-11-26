# Logloom — Progress Log

> Living development journal synchronized with [SPEC.md](./SPEC.md).
> **Agent rule:** Read this file alongside `SPEC.md` at the start of every session. Update it at the end of every completed step before committing.

## Current Status

- **Active Phase:** Phase 1 — C++20 Core Engine & CLI
- **Next Step:** `Step 1.9 — Stable C ABI (c_api.h) & Phase 1 Release (v0.1.0)`
- **Last Completed Step:** `Step 1.8 — Deterministic Log Generator & pread vs mmap Benchmarks`
- **GitHub Remote:** `https://github.com/GorkemSay/logloom`

---

## Step Checklist

### Phase 1 — C++20 Core Engine & CLI
- [x] **Step 1.1** — Project Scaffold, CMake Presets & CI
- [x] **Step 1.2** — Safe Chunked `pread` Reader & Line Framer
- [x] **Step 1.3** — Zero-Copy Streaming JSONL Field Extractor
- [x] **Step 1.4** — Byte-Offset Line Index & O(1) Context Seek
- [x] **Step 1.5** — Streaming Search & Trace Query Engine
- [x] **Step 1.6** — Multithreaded Chunked Search Engine
- [x] **Step 1.7** — `logloom` CLI Tool & Trace Export
- [x] **Step 1.8** — Deterministic Log Generator & `pread` vs `mmap` Benchmarks
- [ ] **Step 1.9** — Stable C ABI (`c_api.h`) & Phase 1 Release (`v0.1.0`)

### Phase 2 — Native macOS Desktop App
- [ ] **Step 2.1** — macOS App Skeleton & `liblogloom` C-ABI Bridge
- [ ] **Step 2.2** — Virtualized Log Viewport (`NSTableView`)
- [ ] **Step 2.3** — Interactive Trace Isolation & Live Search
- [ ] **Step 2.4** — Line Inspector, Keyboard Navigation & Export
- [ ] **Step 2.5** — Instruments Profiling, Polish & `v1.0.0`

---

## Changelog & Engineering Notes

### 2026-11-25 — Step 0: Specification & Workspace Setup
- **Summary:** Initialized repository layout plan, finalized project name (`Logloom`), authored `SPEC.md`, `PROGRESS.md`, and `.gitignore`.
- **Key Decisions:**
  - Core engine in C++20 (`liblogloom`) exposed through a thin C ABI (`c_api.h`) so Phase 2 macOS SwiftUI/AppKit UI shares the exact same engine.
  - Default I/O backend is chunked POSIX `pread` for safety; `mmap` will be implemented as an isolated comparison backend in Step 1.8.
  - Catch2 v3 selected for unit, edge-case, and differential testing under ASan/UBSan.
- **Verification:** Documentation & ignore rules created; no code targets yet.
- **Next:** Execute **Step 1.1** (CMake scaffold, presets, Catch2, smoke test, `README.md`, GitHub Actions CI).

### 2026-11-25 — Step 1.1: Project Scaffold, CMake Presets & CI
- **Files Touched:** `CMakeLists.txt`, `CMakePresets.json`, `include/logloom/version.hpp`, `core/version.cpp`, `core/CMakeLists.txt`, `cli/main.cpp`, `cli/CMakeLists.txt`, `tests/smoke_test.cpp`, `tests/CMakeLists.txt`, `README.md`, `.github/workflows/ci.yml`, `PROGRESS.md`.
- **Summary:**
  - Set up C++20 CMake project (`logloom` 0.1.0) with `debug-asan` and `release` presets (Ninja generator), Catch2 `v3.7.1` via `FetchContent`, and strict `-Wall -Wextra -Wpedantic -Werror` warnings isolated to first-party targets via `logloom_warnings`.
  - Created `liblogloom` static library (`logloom::core`), `logloom` CLI (`--version`/`-V`, `--help`/`-h`, exit code `2` on unknown options), Catch2 smoke test, `README.md`, and macOS + Ubuntu GitHub Actions CI workflow.
- **Key Decisions / Trade-offs:**
  - Isolated compiler warning flags on an `INTERFACE` target (`logloom_warnings`) instead of global `add_compile_options` so third-party Catch2 headers/sources compile cleanly under `-Werror`.
  - Used `CMakePresets.json` schema version `4` for CMake >= 3.24 compatibility across local macOS and CI runners.
- **Verification:**
  - `debug-asan` (AppleClang 21.0.0 + ASan/UBSan): built with 0 warnings; `ctest --preset debug-asan --output-on-failure` passed `1/1` test (`0.02 sec`).
  - `release` (`-O3 -DNDEBUG`): built with 0 warnings; `ctest --preset release --output-on-failure` passed `1/1` test (`0.01 sec`).
  - `./build/release/cli/logloom --version` printed `logloom 0.1.0` (exit `0`); `--unknown` printed error to `stderr` (exit `2`).
- **Next:** Execute **Step 1.2** (Safe Chunked `pread` Reader & Line Framer).
- **Spec Drift / Follow-ups:** None. Public GitHub repository created and `main` pushed to `https://github.com/GorkemSay/logloom`.

### 2026-11-25 — Step 1.2: Safe Chunked `pread` Reader & Line Framer
- **Files Touched:** `include/logloom/posix_file.hpp`, `core/posix_file.cpp`, `include/logloom/line_reader.hpp`, `core/line_reader.cpp`, `core/CMakeLists.txt`, `tests/line_reader_test.cpp`, `tests/CMakeLists.txt`, `PROGRESS.md`.
- **Summary:**
  - Implemented `logloom::PosixFile` as a move-only RAII wrapper opening files strictly read-only (`O_RDONLY | O_CLOEXEC`), rejecting directories (`EISDIR`), and providing `EINTR`-safe `pread_some`/`pread_full`.
  - Implemented `logloom::ChunkedLineReader` with exact `(line_number, byte_offset, byte_length)` tracking, zero-copy fast path within chunks, cross-chunk accumulation, UTF-8 BOM detection/stripping, `\n`/`\r\n` handling, truncated EOF reporting, and bounded-memory `max_line_bytes` enforcement (16 MiB default).
  - Added Catch2 unit, chunk-boundary matrix, BOM, cap-enforcement, and hostile-input tests (`line_reader_test.cpp`).
- **Key Decisions / Trade-offs:**
  - Bounded `line_buf_` to `max_line_bytes + 1` lazily (without upfront cap-sized allocation) so a CRLF line of exact length `max_line_bytes` can retain its trailing `\r` until `\n` confirms stripping, while larger lines drop the buffer immediately and count bytes in O(1) memory.
  - Peeked UTF-8 BOM at offset `0` via `pread_full` during initialization so BOM detection is independent of `chunk_size`.
- **Verification:**
  - `debug-asan` (`AppleClang 21.0.0`, ASan + UBSan): built with 0 warnings; `ctest --preset debug-asan --output-on-failure` passed `8/8` tests (`0.12 sec`).
  - `release` (`-O3 -DNDEBUG`): built with 0 warnings; `ctest --preset release --output-on-failure` passed `8/8` tests (`0.03 sec`).
- **Next:** Execute **Step 1.3** (Zero-Copy Streaming JSONL Field Extractor).
- **Spec Drift / Follow-ups:** None.

### 2026-11-25 — Step 1.3: Zero-Copy Streaming JSONL Field Extractor
- **Files Touched:** `include/logloom/jsonl_extractor.hpp`, `core/jsonl_extractor.cpp`, `core/CMakeLists.txt`, `tests/jsonl_extractor_test.cpp`, `tests/CMakeLists.txt`, `PROGRESS.md`.
- **Summary:**
  - Implemented `logloom::extract_jsonl_fields` as a zero-allocation, non-recursive streaming scanner over `std::string_view` classifying lines into `JsonObject`, `PlainText`, or `Malformed` and extracting top-level `timestamp`, `level`, and configurable `trace_key` (`request_id`) slices.
  - Added iterative fixed-depth (`512`) nested container skipping so nested objects/arrays cannot trigger stack overflow or falsely match nested keys, alongside RFC 8259 string escape, number, and literal validation.
  - Added Catch2 unit, escape, nesting, plain-text, invalid-UTF-8, and adversarial malformed-input tests (`jsonl_extractor_test.cpp`).
- **Key Decisions / Trade-offs:**
  - Replaced recursion with an explicit fixed-size `std::array<ContainerFrame, 512>` state stack in `skip_nested_container` so hostile deeply nested inputs fail deterministically as `Malformed` with O(1) stack and zero heap allocations.
  - Cleared all extracted fields whenever a JSON object fails validation before EOF so callers only consume fields when `kind == LineKind::JsonObject`.
- **Verification:**
  - `debug-asan` (`AppleClang 21.0.0`, ASan + UBSan): built with 0 warnings; `ctest --preset debug-asan --output-on-failure` passed `15/15` tests (`0.27 sec`).
  - `release` (`-O3 -DNDEBUG`): built with 0 warnings; `ctest --preset release --output-on-failure` passed `15/15` tests (`0.03 sec`).
- **Next:** Execute **Step 1.4** (Byte-Offset Line Index & O(1) Context Seek).
- **Spec Drift / Follow-ups:** None.

### 2026-11-25 — Step 1.4: Byte-Offset Line Index & O(1) Context Seek
- **Files Touched:** `include/logloom/line_reader.hpp`, `core/line_reader.cpp`, `include/logloom/line_index.hpp`, `core/line_index.cpp`, `core/CMakeLists.txt`, `tests/line_index_test.cpp`, `tests/CMakeLists.txt`, `PROGRESS.md`.
- **Summary:**
  - Extended `LineReaderOptions` with `start_offset` so `ChunkedLineReader` can resume directly from any indexed byte offset (skipping BOM detection when `start_offset > 0`).
  - Implemented `logloom::LineIndex` with single-pass indexing, O(1) checkpoint seek `read_line`, and single-seek window `read_context`.
  - Added Catch2 tests covering empty/BOM-only files, dense random-access parity, adaptive strided compaction across tiny memory budgets, and `read_context` clamping/oversized-line handling (`line_index_test.cpp`).
- **Key Decisions / Trade-offs:**
  - Chose a default `16 MiB` (`max_memory_bytes`) adaptive strided checkpoint budget: dense (`stride == 1`) up to `2,097,152` lines (`8` bytes per `uint64_t` offset), then compacting in-place and doubling `stride` whenever the budget is reached so multi-GB files stay strictly within bounded memory while keeping random seeks fast.
- **Verification:**
  - `debug-asan` (`AppleClang 21.0.0`, ASan + UBSan): built with 0 warnings; `ctest --preset debug-asan --output-on-failure` passed `19/19` tests (`0.33 sec`).
  - `release` (`-O3 -DNDEBUG`): built with 0 warnings; `ctest --preset release --output-on-failure` passed `19/19` tests (`0.05 sec`).
- **Next:** Execute **Step 1.5** (Streaming Search & Trace Query Engine).
- **Spec Drift / Follow-ups:** None.

### 2026-11-25 — Step 1.5: Streaming Search & Trace Query Engine
- **Files Touched:** `include/logloom/search_engine.hpp`, `core/search_engine.cpp`, `core/CMakeLists.txt`, `tests/search_engine_test.cpp`, `tests/CMakeLists.txt`, `PROGRESS.md`.
- **Summary:**
  - Implemented `logloom::search_file` streaming query engine supporting exact `trace_id` isolation (with configurable `trace_key` and JSON unescape fallback), case-insensitive `level` filtering, case-sensitive/insensitive substring search, optional `std::regex` search, result `limit`, and cooperative `std::atomic<bool>` / callback cancellation.
  - Added full `SearchStats` accounting for scanned lines, matches, JSONL/plain-text/malformed/oversized/truncated-EOF counts, and cancellation/limit flags.
  - Added Catch2 unit and edge-case tests (`search_engine_test.cpp`).
- **Key Decisions / Trade-offs:**
  - Used zero-allocation ASCII case folding for `level` and case-insensitive substring matching, and reused a single unescape buffer only when `has_escapes_in_trace_id` is true.
  - Caught `std::regex_error` during query setup and surfaced it as `std::errc::invalid_argument` so invalid user patterns never throw or crash.
- **Verification:**
  - `debug-asan` (`AppleClang 21.0.0`, ASan + UBSan): built with 0 warnings; `ctest --preset debug-asan --output-on-failure` passed `22/22` tests (`0.41 sec`).
  - `release` (`-O3 -DNDEBUG`): built with 0 warnings; `ctest --preset release --output-on-failure` passed `22/22` tests (`0.21 sec`).
- **Next:** Execute **Step 1.6** (Multithreaded Chunked Search Engine).
- **Spec Drift / Follow-ups:** None.

### 2026-11-26 — Step 1.6: Multithreaded Chunked Search Engine
- **Files Touched:** `include/logloom/line_reader.hpp`, `core/line_reader.cpp`, `include/logloom/search_engine.hpp`, `core/search_engine.cpp`, `tests/parallel_search_test.cpp`, `tests/CMakeLists.txt`, `PROGRESS.md`.
- **Summary:**
  - Added exclusive `end_offset` support to `LineReaderOptions` / `ChunkedLineReader` so partitions can stream bounded byte ranges cleanly.
  - Implemented `logloom::search_file_parallel` with exact newline-boundary partitioning (`partition_file_on_newlines`), a bounded `std::thread` worker pool (`ThreadPoolGuard`), sliding-window in-flight capping (`2 * worker_count`), deterministic file-order batch emission, `limit` early stopping, and cooperative cancellation.
  - Added differential and concurrency Catch2 tests (`parallel_search_test.cpp`) verifying byte-for-byte parity between single-threaded `search_file` and `search_file_parallel` across `{1, 2, 4, 8}` threads, varied partition/chunk sizes, mixed hostile inputs, `limit`, and cancellation.
- **Key Decisions / Trade-offs:**
  - Bounded in-flight partitions with a sliding window (`next_to_claim - next_to_emit < max_in_flight`) and re-extracted `ExtractedFields` on the coordinator right before `on_match` so `std::string_view` slices remain valid while memory stays bounded on large files.
  - Used portable `std::thread` with RAII join guard and per-worker `std::regex` instances to avoid cross-platform `std::jthread` gaps and concurrent regex state sharing.
- **Verification:**
  - `debug-asan` (`AppleClang 21.0.0`, ASan + UBSan): built with 0 warnings; `ctest --preset debug-asan --output-on-failure` passed `25/25` tests (`2.19 sec`).
  - `release` (`-O3 -DNDEBUG`): built with 0 warnings; `ctest --preset release --output-on-failure` passed `25/25` tests (`0.56 sec`).
- **Next:** Execute **Step 1.7** (`logloom` CLI Tool & Trace Export).
- **Spec Drift / Follow-ups:** None.

### 2026-11-26 — Step 1.7: `logloom` CLI Tool & Trace Export
- **Files Touched:** `cli/main.cpp`, `tests/cli_integration_test.cpp`, `tests/CMakeLists.txt`, `README.md`, `PROGRESS.md`.
- **Summary:**
  - Implemented `logloom` CLI subcommands: `trace` (`--id`, `--key`, `--context`, `--export`, `--threads`), `search` (`--query`, `--regex`, `-i`/`--ignore-case`, `--level`, `--limit`, `--threads`), `context` (`--line`, `--before`, `--after`), and `stats` (`--threads`).
  - Enforced clean `stdout`/`stderr` separation, exit codes (`0` match/ok, `1` no match / out of range, `2` usage/regex/IO error), `NO_COLOR`/non-TTY ANSI suppression, and `--export` input-overwrite protection (`std::filesystem::equivalent`).
  - Added end-to-end POSIX `fork`/`execv`/`pipe` integration tests (`cli_integration_test.cpp`) and documented CLI usage in `README.md`.
- **Key Decisions / Trade-offs:**
  - Merged overlapping/adjacent `--context` intervals in `trace` so multi-hit traces stream clean disjoint hunks separated by `--` without duplicating context lines.
  - Used `std::from_chars` for strict unsigned flag parsing and `fork`/`execv`/`poll` in integration tests (zero `system`/`popen` calls).
- **Verification:**
  - `debug-asan` (`AppleClang 21.0.0`, ASan + UBSan): built with 0 warnings; `ctest --preset debug-asan --output-on-failure` passed `28/28` tests (`2.60 sec`).
  - `release` (`-O3 -DNDEBUG`): built with 0 warnings; `ctest --preset release --output-on-failure` passed `28/28` tests (`0.89 sec`).
- **Next:** Execute **Step 1.8** (Deterministic Log Generator & `pread` vs `mmap` Benchmarks).
- **Spec Drift / Follow-ups:** None.

### 2026-11-26 — Step 1.8: Deterministic Log Generator & `pread` vs `mmap` Benchmarks
- **Files Touched:** `include/logloom/mmap_reader.hpp`, `core/mmap_reader.cpp`, `core/search_match_internal.hpp`, `core/search_engine.cpp`, `core/CMakeLists.txt`, `tests/mmap_reader_test.cpp`, `tests/CMakeLists.txt`, `CMakeLists.txt`, `tools/CMakeLists.txt`, `tools/logloom_gen.cpp`, `tools/logloom_bench.cpp`, `tools/run_benchmarks.sh`, `BENCHMARKS.md`, `README.md`, `PROGRESS.md`.
- **Summary:**
  - Added isolated `MmapFile` / `search_file_mmap` comparison backend (`SIGBUS` risk documented; `pread` kept as safe default) and shared matching/pool helpers in `core/search_match_internal.hpp`.
  - Built deterministic seeded generator `tools/logloom-gen`, subprocess benchmark runner `tools/logloom-bench`, and `tools/run_benchmarks.sh`.
  - Measured 100 MiB, 1 GiB, and 5 GiB datasets on Apple M4 (16 GiB RAM, macOS 27.2.1) and documented results in `BENCHMARKS.md` and `README.md`.
- **Key Decisions / Trade-offs:**
  - Verified empirically that `pread-parallel` beats `mmap-parallel` by ~3x at 5 GiB (`1.86–1.94 s` / `2,636–2,758 MiB/s` vs `5.61–6.15 s` / `832–913 MiB/s`) while keeping Peak RSS bounded to `5.61–6.95 MiB` vs `3,895–3,914 MiB` for `mmap-parallel`.
  - Measured `jq -R -c 'fromjson? | ...'` as baseline (`12.45 s` on 1 GiB) since `rg` was not installed on this host.
- **Verification:**
  - `debug-asan` (`AppleClang 21.0.0`, ASan + UBSan): built with 0 warnings; `ctest --preset debug-asan --output-on-failure` passed `31/31` tests (`3.46 sec`).
  - `release` (`-O3 -DNDEBUG`): built with 0 warnings; `ctest --preset release --output-on-failure` passed `31/31` tests (`0.85 sec`).
- **Next:** Execute **Step 1.9** (Stable C ABI `c_api.h` & Phase 1 Release `v0.1.0`).
- **Spec Drift / Follow-ups:** None. Metric 4 (UI scroll hitch rate) is deferred to Phase 2 / Step 2.5 per `SPEC.md`.

<!--
Entry template for subsequent steps:

### YYYY-MM-DD — Step X.Y: <Title>
- **Commit:** `<short-sha>`
- **Summary:** <1–3 concise bullets of what was implemented>
- **Key Decisions / Trade-offs:** <design choices & why>
- **Verification:** <build presets, ctest pass count, sanitizers, or benchmark numbers>
- **Spec Drift / Follow-ups:** <none, or explicit note if anything deferred>
-->
