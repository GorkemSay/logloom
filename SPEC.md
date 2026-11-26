# Logloom — Engineering Specification

> Single source of truth for **Logloom**: a high-performance, local-first log & JSONL trace inspector built on a C++20 core engine with a native macOS desktop app.

---

## 1. Purpose & Core Principles

**Problem:** Developers investigating production incidents in multi-gigabyte `.jsonl` / `.log` files hit freezes in editors (VS Code, Excel) or lose context jumping between `rg` and `jq`.

**Core User Flow:** Drop a 5 GB JSONL log file → inspect lines immediately → click or query a `request_id` to isolate that request's timeline → jump to surrounding ±N context lines in O(1) → export only the relevant trace.

**Non-Negotiable Principles:**
1. **Fast & Measurable:** Optimize based on measurements, not guesses. Never publish unverified performance claims.
2. **Resilient:** Never crash, hang, or OOM on malformed JSON, invalid UTF-8, huge single lines, or truncated files.
3. **Clean Architecture:** Core I/O, indexing, and search live in a UI-agnostic C++20 library (`liblogloom`) exposed via a thin C ABI (`logloom_c.h`). CLI and macOS UI are thin consumers.
4. **Zero AI Slop:** Clean, restrained, native craft in both CLI output and macOS UI. No gimmicky gradients, emoji clutter, or over-engineered abstractions.

---

## 2. Target Architecture & Repository Layout

```text
logloom/
├── SPEC.md                    # Authoritative specification (this file)
├── PROGRESS.md                # Living step-by-step execution log
├── README.md                  # Public overview, build guide, verified benchmarks
├── BENCHMARKS.md              # Reproducible benchmark methodology & results (Step 1.8)
├── CMakeLists.txt
├── CMakePresets.json          # debug-asan, release
├── .github/workflows/ci.yml   # macOS + Linux build & sanitizer tests
├── include/logloom/
│   ├── ...                    # Public C++20 headers
│   └── c_api.h                # Thin, ABI-stable C bridge for Swift/macOS (Step 1.9)
├── core/                      # liblogloom static library (C++20, zero UI deps)
├── cli/                       # logloom CLI binary
├── tools/                     # Deterministic synthetic log generator & bench runner
├── tests/                     # Catch2 unit, edge-case, integration & C-ABI tests
└── macos/                     # Phase 2: Native SwiftUI + AppKit app linking liblogloom
```

**Tech Stack:**
- **Phase 1 (Core + CLI):** C++20, CMake (≥3.24), Catch2 v3 (via `FetchContent`), POSIX I/O (`pread`, optional isolated `mmap` backend for comparison). No heavy third-party JSON DOM libraries in the hot path.
- **Phase 2 (macOS App):** Swift Package Manager (`Package.swift`, no `.xcodeproj` required), AppKit (`NSTableView` for virtualized scrolling), SwiftUI (window chrome, toolbar, inspector), linking `liblogloom` via `c_api.h` + a `.app` bundle & signing script.

---

## 3. Stability, Security & Safety Requirements

1. **Compiler & Memory Hygiene:**
   - Enable `-Wall -Wextra -Wpedantic -Werror` on all first-party targets.
   - Use RAII for file descriptors, memory mappings, and buffers; zero raw owning pointers.
   - All tests must pass under **AddressSanitizer + UndefinedBehaviorSanitizer (`debug-asan` preset)**.
2. **Hostile & Edge-Case Input Guarantees:**
   - **Bounded memory on huge lines:** Enforce a configurable maximum single-line buffer cap (default **16 MiB**). Lines exceeding the cap must be skipped/flagged safely without OOM or abort.
   - **Truncated / active files:** Handle missing trailing `\n`, mid-JSON EOF, and short `pread` reads gracefully; report truncated final line explicitly in metadata/diagnostics.
   - **Encoding & newlines:** Handle `\n`, `\r\n`, empty files, UTF-8 BOM, invalid UTF-8 byte sequences, and mixed plain-text + JSONL lines without crashing.
   - **`mmap` safety:** Keep `pread` as the safe default backend. Isolate `mmap` behind an explicit backend option/benchmark and document its `SIGBUS` / file-truncation risk.
3. **Security:**
   - Strictly **read-only** on input log files (never modify user logs).
   - No shell invocation (`system`/`popen`), no arbitrary code evaluation, no network/telemetry calls.
   - Phase 2 macOS app must run under **App Sandbox** using security-scoped bookmarks / user-selected read-only file access.

---

## 4. Testing & Benchmark Discipline

- **Test Suite (Catch2):**
  - Every module gets focused unit tests + hostile edge-case tests (empty file, 1-byte file, exact chunk-boundary newlines, `\r\n`, escaped quotes `\"`, nested keys with same name as trace key, truncated JSON at EOF, >cap line, invalid UTF-8).
  - Differential tests: verify multithreaded search matches single-threaded search byte-for-byte in file order.
  - C-ABI tests: verify handle lifecycle and zero leaks under ASan.
- **Benchmark Discipline (Step 1.8):**
  - Include a deterministic, seeded generator (`tools/logloom-gen`) so anyone can reproduce 100 MB / 1 GB / 5 GB datasets without committing data files to Git.
  - Measure and report **4 distinct metrics** separately (with hardware, OS, and cold vs. warm cache noted):
    1. Initial readable screen / index build time
    2. Time to first match & full scan duration (plus throughput in MB/s)
    3. Peak memory usage (Peak RSS)
    4. *(Phase 2)* Scroll hitch/frame drop rate during active background search
  - Compare chunked `pread` vs. `mmap` on the same dataset and hardware; include `rg` / `jq` as an honest reference baseline where applicable.

---

## 5. Design Direction (No AI Slop)

- **CLI (Phase 1):**
  - Data goes to `stdout`, progress/warnings/errors go to `stderr`.
  - Clean exit codes (`0` match/ok, `1` no matches, `2` usage/IO error).
  - Respect `NO_COLOR` and non-TTY piping (no ANSI escapes when piped).
- **macOS UI (Phase 2):**
  - Native macOS aesthetic (system light/dark palette, SF Pro UI chrome, SF Mono with tabular numerals for timestamps/line numbers/offsets).
  - Virtualized `NSTableView` viewport so multi-GB logs scroll smoothly without freezing the main thread.
  - Keyboard-first workflow (`⌘F` search, `⌘G`/`⇧⌘G` next/prev match, `⌘L` jump to line, `Esc` clear/cancel).
  - Zero decorative gradients, neon accents, marketing fluff, or emoji-heavy chrome.

---

## 6. Agent Operating Protocol & Git Rules

> Follow this loop strictly for every step. Do **not** combine multiple steps into one commit.

1. **Sync First:** Read `SPEC.md` and `PROGRESS.md` to confirm the exact next step and prior decisions.
2. **Scoped Execution:** Implement **only** the current step. Keep diffs clean, minimal, and idiomatic.
3. **Verify Before Done:**
   - Build both `debug-asan` and `release` presets with zero warnings.
   - Run full test suite (`ctest --output-on-failure`) under `debug-asan`.
4. **Update Docs:**
   - Append a concise entry to `PROGRESS.md` (date, step ID, files touched, tests/metrics verified, key decisions, commit hash/next step).
   - Update `README.md` whenever build commands, CLI flags, or verified benchmark numbers change.
5. **Git Commit & Push (Mandatory After Each Completed Step):**
   - Run `git status` and confirm **no** build artifacts (`build/`), binaries, or generated `.jsonl`/`.log` files are staged.
   - Commit using Conventional Commits (e.g., `feat(core): add chunked pread file reader and line framer`).
   - Push to the active GitHub remote branch (`git push`).
   - *Safety:* Never force-push. If no GitHub remote is configured yet, commit locally and explicitly remind the user to set the remote URL.

---

## 7. Step-by-Step Roadmap

### Phase 1 — C++20 Core Engine & CLI

#### Step 1.1 — Project Scaffold, CMake Presets & CI
- Set up `CMakeLists.txt`, `CMakePresets.json` (`debug-asan` with ASan/UBSan, `release` with `-O3`), Catch2 v3 via `FetchContent`.
- Create minimal skeleton for `liblogloom`, `logloom` CLI (`--version`), smoke test, initial `README.md`, and `.github/workflows/ci.yml` (macOS + Linux).
- **Done when:** `debug-asan` & `release` build cleanly, smoke test passes, CI workflow is valid.

#### Step 1.2 — Safe Chunked `pread` Reader & Line Framer
- Implement RAII POSIX read-only file handle and chunked `pread` line reader (`ChunkedLineReader`).
- Track exact `(line_number, byte_offset, byte_length)` for every line.
- Handle `\n`, `\r\n`, UTF-8 BOM, empty files, lines crossing chunk boundaries, huge-line cap (16 MiB default), and truncated final line (EOF without `\n`).
- **Done when:** Thorough unit + boundary/hostile-input tests pass under ASan/UBSan.

#### Step 1.3 — Zero-Copy Streaming JSONL Field Extractor
- Implement a lightweight streaming scanner over `std::string_view` that extracts top-level JSON fields (`timestamp`, `level`, and a configurable trace key defaulting to `request_id`) without building a DOM tree.
- Correctly handle whitespace, escaped quotes (`\"`, `\\`), nested objects/arrays (must NOT falsely match keys inside nested objects or string values), and non-JSON/malformed lines (mark as plain text or malformed without failing).
- **Done when:** Comprehensive unit + adversarial JSONL test suite passes under ASan/UBSan.

#### Step 1.4 — Byte-Offset Line Index & O(1) Context Seek
- Build a compact `LineIndex` storing byte offsets (dense for small files or strided sparse checkpoints with bounded memory for multi-GB files — document the chosen memory budget).
- Support `read_line(index)` and `read_context(index, before, after)` using O(1) `pread` seeks without rescanning from start.
- **Done when:** Indexing + random-access line/context tests pass accurately on files with CRLF, empty lines, and truncated EOF.

#### Step 1.5 — Streaming Search & Trace Query Engine
- Implement query engine supporting: exact trace-key match (`request_id`), log `level` filter, case-sensitive/insensitive substring search, and optional regex search.
- Stream results via callback as matches are found (enabling instant time-to-first-match) with cooperative `std::atomic<bool>` cancellation.
- Report truncated/malformed line counts in a `SearchStats` summary.
- **Done when:** Query, streaming callback, cancellation, and edge-case tests pass.

#### Step 1.6 — Multithreaded Chunked Search Engine
- Partition large files on exact newline boundaries across a bounded worker thread pool (`std::jthread` / `std::thread`).
- Merge and emit results in deterministic file order (or ordered batch streaming) with cancellation support.
- **Done when:** Differential tests confirm multithreaded results match single-threaded results identically across varied chunk sizes and thread counts; zero races/leaks.

#### Step 1.7 — `logloom` CLI Tool & Trace Export
- Implement subcommands:
  - `logloom trace <file> --id <value> [--key request_id] [--context N] [--export out.jsonl]`
  - `logloom search <file> --query <text> [--regex] [--level ERROR] [--limit N]`
  - `logloom context <file> --line <N> [--before 10] [--after 10]`
  - `logloom stats <file>` (size, line count, valid JSONL vs malformed, truncated EOF status)
- Clean stdout/stderr separation, exit codes, `NO_COLOR`/TTY handling.
- **Done when:** End-to-end CLI integration tests cover all subcommands, flags, export, and error paths; `README.md` documents usage.

#### Step 1.8 — Deterministic Log Generator & `pread` vs `mmap` Benchmarks
- Build `tools/logloom-gen` to deterministically generate seeded JSONL datasets (100 MB, 1 GB, 5 GB) with realistic `request_id` traces, noise, errors, and optional truncated EOF.
- Add an isolated `mmap` read backend alongside `pread`.
- Run and document reproducible benchmarks in `BENCHMARKS.md` (and summarize in `README.md`): cold vs. warm cache, `pread` vs. `mmap`, single vs. multi-thread, plus `rg` reference baseline across the 4 metrics in Section 4.
- **Done when:** Generator + benchmark scripts run cleanly and `BENCHMARKS.md` contains real measured numbers and hardware/cache notes.

#### Step 1.9 — Stable C ABI (`c_api.h`) & Phase 1 Release (`v0.1.0`)
- Expose opaque-handle C ABI (`include/logloom/c_api.h`) for: open/close file, incremental indexing with progress callback, streaming search/trace with cancel token, O(1) line & context fetch, and error string retrieval.
- Write pure-C caller tests verifying full lifecycle and zero leaks under ASan.
- Final Phase 1 audit of `README.md` and `PROGRESS.md`; tag `v0.1.0`.

---

### Phase 2 — Native macOS Desktop App (SwiftUI + AppKit)

*(Detailed UI sub-specs may be refined right before Step 2.1 based on Phase 1 learnings.)*

- **Step 2.1 — macOS App Skeleton & `liblogloom` C-ABI Bridge:** SwiftPM (`Package.swift`) executable linking `liblogloom` via `c_api.h` + `.app` bundle/entitlements script (`Logloom.app`, App Sandbox), Swift wrapper types, drag-and-drop & Open Panel read-only file loading.
- **Step 2.2 — Virtualized Log Viewport (`NSTableView`):** Instant first-screen render + background indexing progress + smooth virtual scrolling + truncated-EOF banner.
- **Step 2.3 — Interactive Trace Isolation & Live Search:** Click any `request_id` to view its chronological timeline; non-blocking search with cancel-on-type.
- **Step 2.4 — Line Inspector, Keyboard Navigation & Export:** Split detail pane for formatted JSON inspection, ±N context jump, keyboard shortcuts, and filtered `.jsonl` export.
- **Step 2.5 — Profiling, Polish & `v1.0.0`:** Profile CPU, peak RSS, and scroll hitches via `xcrun xctrace` / Instruments; refine visuals; record concise demo GIF and update `README.md`.
