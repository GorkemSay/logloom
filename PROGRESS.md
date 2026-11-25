# Logloom — Progress Log

> Living development journal synchronized with [SPEC.md](./SPEC.md).
> **Agent rule:** Read this file alongside `SPEC.md` at the start of every session. Update it at the end of every completed step before committing.

## Current Status

- **Active Phase:** Phase 1 — C++20 Core Engine & CLI
- **Next Step:** `Step 1.3 — Zero-Copy Streaming JSONL Field Extractor`
- **Last Completed Step:** `Step 1.2 — Safe Chunked pread Reader & Line Framer`
- **GitHub Remote:** `https://github.com/GorkemSay/logloom`

---

## Step Checklist

### Phase 1 — C++20 Core Engine & CLI
- [x] **Step 1.1** — Project Scaffold, CMake Presets & CI
- [x] **Step 1.2** — Safe Chunked `pread` Reader & Line Framer
- [ ] **Step 1.3** — Zero-Copy Streaming JSONL Field Extractor
- [ ] **Step 1.4** — Byte-Offset Line Index & O(1) Context Seek
- [ ] **Step 1.5** — Streaming Search & Trace Query Engine
- [ ] **Step 1.6** — Multithreaded Chunked Search Engine
- [ ] **Step 1.7** — `logloom` CLI Tool & Trace Export
- [ ] **Step 1.8** — Deterministic Log Generator & `pread` vs `mmap` Benchmarks
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

<!--
Entry template for subsequent steps:

### YYYY-MM-DD — Step X.Y: <Title>
- **Commit:** `<short-sha>`
- **Summary:** <1–3 concise bullets of what was implemented>
- **Key Decisions / Trade-offs:** <design choices & why>
- **Verification:** <build presets, ctest pass count, sanitizers, or benchmark numbers>
- **Spec Drift / Follow-ups:** <none, or explicit note if anything deferred>
-->
