# Logloom — Progress Log

> Living development journal synchronized with [SPEC.md](./SPEC.md).
> **Agent rule:** Read this file alongside `SPEC.md` at the start of every session. Update it at the end of every completed step before committing.

## Current Status

- **Active Phase:** Phase 2 — Native macOS Desktop App
- **Next Step:** `Step 2.4 — Line Inspector, Keyboard Navigation & Export`
- **Last Completed Step:** `Step 2.3 — Interactive Trace Isolation & Live Search`
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
- [x] **Step 1.9** — Stable C ABI (`c_api.h`) & Phase 1 Release (`v0.1.0`)

### Phase 2 — Native macOS Desktop App
- [x] **Step 2.1** — macOS App Skeleton & `liblogloom` C-ABI Bridge
- [x] **Step 2.2** — Virtualized Log Viewport (`NSTableView`)
- [x] **Step 2.3** — Interactive Trace Isolation & Live Search
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

### 2026-11-26 — Step 1.9: Stable C ABI (`c_api.h`) & Phase 1 Release (`v0.1.0`)
- **Files Touched:** `CMakeLists.txt`, `include/logloom/line_index.hpp`, `core/line_index.cpp`, `include/logloom/c_api.h`, `core/c_api.cpp`, `core/CMakeLists.txt`, `tests/c_api_test.c`, `tests/CMakeLists.txt`, `README.md`, `PROGRESS.md`.
- **Summary:**
  - Extended `LineIndex::build` with optional `IndexProgressCallback` and `std::atomic<bool>` cancellation.
  - Designed and implemented the C11-compatible opaque-handle C ABI (`include/logloom/c_api.h`, `core/c_api.cpp`) covering file open/close/size, incremental indexing with progress & cancellation, O(1) line & context retrieval, streaming parallel search/trace, and thread-safe error strings.
  - Added pure-C11 caller test suite (`tests/c_api_test.c`) exercising full handle lifecycle, error paths, cancellation, and zero memory leaks under ASan/UBSan.
  - Audited `README.md` and `PROGRESS.md` for Phase 1 `v0.1.0` completion and tagged `v0.1.0`.
- **Key Decisions / Trade-offs:**
  - Guarded every `extern "C"` entrypoint with `try/catch` and duplicated NUL-terminated scratch strings before invoking `logloom_match_cb` so both C and upcoming Phase 2 Swift callers can safely use either C-string APIs or `(ptr, length)` slices.
  - Replaced `assert(...)` with a release-active `TEST_CHECK(...)` macro in `tests/c_api_test.c` so `-DNDEBUG` release builds still execute every C-ABI assertion cleanly under `-Werror`.
- **Verification:**
  - `debug-asan` (`AppleClang 21.0.0`, ASan + UBSan): built with 0 warnings; `ctest --preset debug-asan --output-on-failure` passed `32/32` tests (`3.20 sec`).
  - `release` (`-O3 -DNDEBUG`): built with 0 warnings; `ctest --preset release --output-on-failure` passed `32/32` tests (`1.13 sec`).
- **Next:** Execute **Step 2.1** (macOS App Skeleton & `liblogloom` C-ABI Bridge via SwiftPM).
- **Spec Drift / Follow-ups:** None. Phase 1 complete and tagged `v0.1.0`.

### 2026-11-26 — Phase 1 Post-Release Audit & Phase 2 C-ABI Hardening
- **Files Touched:** `core/search_engine.cpp`, `core/mmap_reader.cpp`, `core/line_index.cpp`, `core/posix_file.cpp`, `include/logloom/c_api.h`, `core/c_api.cpp`, `cli/main.cpp`, `tests/parallel_search_test.cpp`, `tests/line_reader_test.cpp`, `tests/cli_integration_test.cpp`, `tests/c_api_test.c`, `SPEC.md`, `README.md`, `PROGRESS.md`.
- **Summary:**
  - Skipped `OwnedMatch` string materialization in `search_file_parallel` and `search_file_mmap` when `on_match` is empty (zero unnecessary allocations for `logloom stats`), preserving exact match counts and `limit` semantics.
  - Added Phase 2 C-ABI primitives: `logloom_file_read_head` (instant pre-index first-screen preview) and zero-copy `logloom_extract_fields`, and removed `const_cast` in `core/c_api.cpp` via `mutable last_error`.
  - Opened input file before `--export` target in `logloom trace` so missing input never creates/truncates the export file; capped `LineIndex` checkpoint vector growth to `max_checkpoints`; rejected non-regular files (`!S_ISREG`) in `PosixFile::open_read_only`; aligned `SPEC.md` (`c_api.h`).
- **Verification:**
  - `debug-asan` (`AppleClang 21.0.0`, ASan + UBSan): built with 0 warnings; `ctest --preset debug-asan --output-on-failure` passed `32/32` tests (`3.33 sec`).
  - `release` (`-O3 -DNDEBUG`): built with 0 warnings; `ctest --preset release --output-on-failure` passed `32/32` tests (`1.06 sec`).
- **Next:** Execute **Step 2.1** (macOS App Skeleton & `liblogloom` C-ABI Bridge via SwiftPM).

### 2026-11-26 — Step 2.1: macOS App Skeleton & `liblogloom` C-ABI Bridge
- **Files Touched:** `CMakeLists.txt`, `.gitignore`, `.github/workflows/ci.yml`, `README.md`, `PROGRESS.md`, `macos/Package.swift`, `macos/Sources/CLogloom/*`, `macos/Sources/LogloomCore/*`, `macos/Sources/LogloomApp/*`, `macos/Resources/Info.plist`, `macos/Resources/Logloom.entitlements`, `macos/scripts/bundle_app.sh`, `macos/Tests/LogloomCoreTests/LogloomCoreTests.swift`.
- **Summary:**
  - Created SwiftPM package (`macos/Package.swift`, macOS 14+) exposing `CLogloom` (`c_api.h` bridge), `LogloomCore` (`LogFile`, `CancelToken`, `LogLine`, `LogloomError` RAII Swift wrappers), and `LogloomApp` (SwiftUI/AppKit skeleton with `NSOpenPanel`, Finder file-open delegate, drag-and-drop, security-scoped resource guard, and initial pre-index `readHead` preview).
  - Added App Sandbox entitlements (`com.apple.security.app-sandbox`, `files.user-selected.read-only`, `files.bookmarks.app-scope`), `Info.plist`, and `macos/scripts/bundle_app.sh` producing signed `build/macos/Logloom.app`.
  - Set `CMAKE_OSX_DEPLOYMENT_TARGET=14.0` on Apple so `liblogloom.a` links cleanly with SwiftPM without deployment-target warnings; updated macOS CI job.
- **Key Decisions / Trade-offs:**
  - Used SwiftPM + a reproducible `.app` bundle/signing script instead of `.xcodeproj`, avoiding brittle `.pbxproj` maintenance while still enforcing App Sandbox entitlements via `codesign`.
- **Verification:**
  - `debug-asan` (`32/32` C/C++ tests) & `release` (`32/32` C/C++ tests) built and passed with 0 warnings.
  - `swift test --package-path macos` (`3/3` `LogloomCoreTests`) passed with 0 warnings.
  - `./macos/scripts/bundle_app.sh` built and ad-hoc signed `build/macos/Logloom.app` (`codesign --verify` passed, App Sandbox entitlements verified).
- **Next:** Execute **Step 2.2** (Virtualized Log Viewport via `NSTableView`).
- **Spec Drift / Follow-ups:** None.

### 2026-11-26 — Step 2.2: Virtualized Log Viewport (NSTableView)
- **Files Touched:** `macos/Sources/LogloomCore/LinePageCache.swift`, `macos/Sources/LogloomApp/AppModel.swift`, `macos/Sources/LogloomApp/VirtualizedLogTableView.swift`, `macos/Sources/LogloomApp/ContentView.swift`, `macos/Tests/LogloomCoreTests/LogloomCoreTests.swift`, `PROGRESS.md`.
- **Summary:**
  - Implemented `LinePageCache` (256-line pages, 32-page bounded LRU eviction) serving instant pre-index `readHead(maxLines: 500)` rows immediately and lazily faulting random-access pages via `LogFile.readContext` once indexing finishes.
  - Updated `AppModel` with `CancelToken` lifecycle management, `openGeneration` guards against stale completions, throttled (~50 ms) background indexing progress updates, truncated-EOF & oversized-line metadata, and selection tracking.
  - Built `VirtualizedLogTableView` wrapping AppKit `NSScrollView` + `NSTableView` (fixed 20 px rows, 5 tabular monospaced columns `#`, `Timestamp`, `Level`, `Trace ID`, `Content`, reusable `NSTableCellView` + `NSTextField` cells, scroll/selection preservation across index completion).
  - Updated `ContentView` with a native linear indexing progress indicator, dismissible truncated-EOF banner, and bottom status bar.
- **Key Decisions / Trade-offs:**
  - Faulted full 256-line pages in one `LogFile.readContext` C-ABI call per cache miss rather than 256 per-row `readLine` calls, avoiding repeated seek/parse overhead while bounding resident UI line memory to ~8,192 lines.
- **Verification:**
  - `debug-asan` (`32/32` C/C++ tests) & `release` (`32/32` C/C++ tests) passed with 0 warnings.
  - `swift test --package-path macos` (`4/4` `LogloomCoreTests`, including `testLinePageCachePreIndexAndBoundedLRUEviction`) passed with 0 warnings.
  - `./macos/scripts/bundle_app.sh` built and ad-hoc signed `build/macos/Logloom.app` cleanly.
- **Next:** Execute **Step 2.3** (Interactive Trace Isolation & Live Search).
- **Spec Drift / Follow-ups:** None.

### 2026-11-26 — Step 2.3: Interactive Trace Isolation & Live Search
- **Files Touched:** `macos/Sources/LogloomCore/LogLine.swift`, `macos/Sources/LogloomCore/ViewportStore.swift`, `macos/Sources/LogloomApp/AppModel.swift`, `macos/Sources/LogloomApp/VirtualizedLogTableView.swift`, `macos/Sources/LogloomApp/ContentView.swift`, `macos/Sources/LogloomApp/LogloomApp.swift`, `macos/Tests/LogloomCoreTests/LogloomCoreTests.swift`, `PROGRESS.md`.
- **Summary:**
  - Added `ViewportStore` seamlessly switching between `LinePageCache` (`.fullFile` mode) and streamed query matches (`.filtered` mode), with binary-search `rowIndex(forLineNumber:)` selection mapping across modes.
  - Updated `AppModel` for non-blocking live search (`120 ms` debounce + cancel-on-type via `CancelToken`, `searchGeneration` staleness guards, batched streaming flushes up to `100,000` matches, level filtering, case-sensitivity, regex mode, and `isolateTrace`/`clearTraceIsolation`).
  - Updated `VirtualizedLogTableView` with clickable link-styled `Trace ID` cells, context-menu actions (`Isolate Trace` / `Clear Trace Isolation`), and mode-switch selection scrolling (jumping back to the selected line when exiting trace isolation).
  - Updated `LogloomApp` and `ContentView` with `⌘F` focus shortcut, `Esc` clear/exit command, search/filter bar (`Aa`, `.*`, level picker), match counter/timing badge, regex error banner, and trace isolation header strip.
- **Key Decisions / Trade-offs:**
  - Batched streaming matches on the background worker thread (`200` matches or `50 ms` cadence) before dispatching to `@MainActor` so high-volume queries update incrementally without flooding SwiftUI/AppKit table reloads.
  - Applied an interactive cap of `100,000` filtered matches in UI memory while leaving `fullFile` mode backed by bounded LRU `LinePageCache`.
- **Verification:**
  - `debug-asan` (`32/32` C/C++ tests) & `release` (`32/32` C/C++ tests) passed with 0 warnings.
  - `swift test --package-path macos` (`6/6` `LogloomCoreTests`, including `testViewportStoreFullAndFilteredModes` and `testTraceIsolationAndInvalidRegex`) passed with 0 warnings.
  - `./macos/scripts/bundle_app.sh` built and ad-hoc signed `build/macos/Logloom.app` cleanly.
- **Next:** Execute **Step 2.4** (Line Inspector, Keyboard Navigation & Export).
- **Spec Drift / Follow-ups:** None.

<!--
Entry template for subsequent steps:

### YYYY-MM-DD — Step X.Y: <Title>
- **Commit:** `<short-sha>`
- **Summary:** <1–3 concise bullets of what was implemented>
- **Key Decisions / Trade-offs:** <design choices & why>
- **Verification:** <build presets, ctest pass count, sanitizers, or benchmark numbers>
- **Spec Drift / Follow-ups:** <none, or explicit note if anything deferred>
-->
