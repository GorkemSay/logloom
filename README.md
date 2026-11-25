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

## Project Documentation

- [SPEC.md](./SPEC.md) — Engineering specification and step-by-step roadmap
- [PROGRESS.md](./PROGRESS.md) — Living development journal and status log
