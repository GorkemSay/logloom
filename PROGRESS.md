# Logloom — Progress Log

> Living development journal synchronized with [SPEC.md](./SPEC.md).
> **Agent rule:** Read this file alongside `SPEC.md` at the start of every session. Update it at the end of every completed step before committing.

## Current Status

- **Active Phase:** Phase 1 — C++20 Core Engine & CLI
- **Next Step:** `Step 1.2 — Safe Chunked pread Reader & Line Framer`
- **Last Completed Step:** `Step 1.1 — Project Scaffold, CMake Presets & CI`
- **GitHub Remote:** *(Pending — GitHub API returned HTTP 500 during `partial_outage`; local commits ready to push)*

---

## Step Checklist

### Phase 1 — C++20 Core Engine & CLI
- [x] **Step 1.1** — Project Scaffold, CMake Presets & CI
- [ ] **Step 1.2** — Safe Chunked `pread` Reader & Line Framer
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
- **Spec Drift / Follow-ups:** GitHub remote creation via `gh` returned HTTP 500 during an active GitHub `Git Operations` partial outage; Step 1.1 is committed locally and ready to push once GitHub recovers.

<!--
Entry template for subsequent steps:

### YYYY-MM-DD — Step X.Y: <Title>
- **Commit:** `<short-sha>`
- **Summary:** <1–3 concise bullets of what was implemented>
- **Key Decisions / Trade-offs:** <design choices & why>
- **Verification:** <build presets, ctest pass count, sanitizers, or benchmark numbers>
- **Spec Drift / Follow-ups:** <none, or explicit note if anything deferred>
-->
