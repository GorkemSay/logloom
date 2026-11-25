# Logloom — Progress Log

> Living development journal synchronized with [SPEC.md](./SPEC.md).
> **Agent rule:** Read this file alongside `SPEC.md` at the start of every session. Update it at the end of every completed step before committing.

## Current Status

- **Active Phase:** Phase 1 — C++20 Core Engine & CLI
- **Next Step:** `Step 1.1 — Project Scaffold, CMake Presets & CI`
- **Last Completed Step:** `Step 0 — Specification & Workspace Setup`
- **GitHub Remote:** *(Not yet linked — set remote before first push)*

---

## Step Checklist

### Phase 1 — C++20 Core Engine & CLI
- [ ] **Step 1.1** — Project Scaffold, CMake Presets & CI
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

<!--
Entry template for subsequent steps:

### YYYY-MM-DD — Step X.Y: <Title>
- **Commit:** `<short-sha>`
- **Summary:** <1–3 concise bullets of what was implemented>
- **Key Decisions / Trade-offs:** <design choices & why>
- **Verification:** <build presets, ctest pass count, sanitizers, or benchmark numbers>
- **Spec Drift / Follow-ups:** <none, or explicit note if anything deferred>
-->
