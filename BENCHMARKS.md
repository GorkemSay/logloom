# Logloom — Benchmarks & Backend Analysis

Reproducible benchmark methodology, hardware/cache notes, and real measured comparisons between Logloom's default chunked `pread` backend, its isolated `mmap` comparison backend, and `jq`.

## Test Environment & Methodology

- **Hardware:** Apple M4, 10 logical cores, 16 GiB RAM (`17,179,869,184` bytes), internal Apple SSD
- **OS:** macOS `27.2.1` (Build `26B131`)
- **Compiler & Preset:** AppleClang `21.0.0`, CMake `release` preset (`-O3 -DNDEBUG`)
- **Datasets:** Deterministically generated via `tools/logloom-gen` (`--seed 42 --target-trace req-target-001 --trace-hits 50`):
  - **100 MiB:** `104,857,772` bytes (`537,415` lines, `50` trace hits, ~1% plain-text noise, ~0.2% malformed JSON)
  - **1 GiB:** `1,073,741,882` bytes (`5,502,862` lines, `50` trace hits)
  - **5 GiB:** `5,368,709,150` bytes (`27,514,116` lines, `50` trace hits)
- **Cache Conditions:**
  - **Pass 1** runs in fresh subprocesses immediately after `logloom-gen` writes the file (`sudo purge` was not used, so OS page cache may hold recently written pages on 100 MiB / 1 GiB, while 5 GiB exceeds free cache headroom and triggers page pressure).
  - **Pass 2** runs immediately after Pass 1 (warm page cache).
- **Units:** `1 MiB = 1,048,576` bytes. Throughput is reported in `MiB/s`. Peak RSS is measured per-process via `getrusage(RUSAGE_SELF)` (`ru_maxrss` in bytes on macOS) or `/usr/bin/time -l` for `jq`.
- **Reference Baseline:** `rg` was not installed on this machine; `/usr/bin/jq` was measured with `jq -R -c 'fromjson? | select(.request_id == "req-target-001")'` (`fromjson?` is required because plain `jq` aborts on the dataset's ~0.2% malformed JSON lines).

### Reproduce Locally

```bash
cmake --preset release
cmake --build --preset release
./tools/run_benchmarks.sh
```

---

## Metrics Summary (Section 4 Coverage)

1. **Metric 1 — Initial Index Build (`LineIndex::build`):** Single-pass offset indexing with 16 MiB checkpoint budget.
2. **Metric 2 — Time to First Match (TTFM) & Full Scan Throughput (`MiB/s`):** Measured from query start to first `on_match` callback and total scan completion.
3. **Metric 3 — Peak Memory Usage (Peak RSS):** Measured via `ru_maxrss` in isolated subprocesses.
4. **Metric 4 — UI Scroll Hitch / Frame Drop Rate:** Verified via Apple Instruments (`xctrace` `Time Profiler` and `Animation Hitches`) in the [macOS Desktop App Profiling](#macos-desktop-app-profiling-instruments--xctrace) section below.

---

## Measured Results

### 100 MiB (`104,857,772` bytes, `537,415` lines)

| Mode | Pass 1 Duration | Pass 1 TTFM | Pass 1 Throughput | Pass 2 Duration | Pass 2 TTFM | Pass 2 Throughput | Peak RSS | Notes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `index` | `23.38 ms` | `N/A` | `4,277.51 MiB/s` | `15.90 ms` | `N/A` | `6,288.10 MiB/s` | `14.16 MiB` | `stride=1`, `index_mem=4.10 MiB` |
| `pread-single` | `89.12 ms` | `1.25 ms` | `1,122.09 MiB/s` | `89.41 ms` | `0.85 ms` | `1,118.48 MiB/s` | `1.97 MiB` | `50` matches |
| `pread-parallel` | `16.15 ms` | `6.43 ms` | `6,193.08 MiB/s` | `16.54 ms` | `7.65 ms` | `6,044.42 MiB/s` | `5.33–5.56 MiB` | `50` matches |
| `mmap-single` | `80.57 ms` | `1.26 ms` | `1,241.10 MiB/s` | `82.47 ms` | `1.33 ms` | `1,212.59 MiB/s` | `101.70 MiB` | `50` matches |
| `mmap-parallel` | `16.70 ms` | `9.98 ms` | `5,986.41 MiB/s` | `15.73 ms` | `7.11 ms` | `6,355.93 MiB/s` | `101.95 MiB` | `50` matches |
| `jq -R fromjson?` | `1,170.00 ms` | — | `85.47 MiB/s` | — | — | — | `2.42 MiB` | `/usr/bin/time -l` (`1.17 s` real) |

### 1 GiB (`1,073,741,882` bytes, `5,502,862` lines)

| Mode | Pass 1 Duration | Pass 1 TTFM | Pass 1 Throughput | Pass 2 Duration | Pass 2 TTFM | Pass 2 Throughput | Peak RSS | Notes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `index` | `158.92 ms` | `N/A` | `6,443.68 MiB/s` | `130.14 ms` | `N/A` | `7,868.62 MiB/s` | `34.05–34.06 MiB` | `stride=4`, `index_mem=10.50 MiB` |
| `pread-single` | `862.16 ms` | `8.57 ms` | `1,187.71 MiB/s` | `858.81 ms` | `8.21 ms` | `1,192.34 MiB/s` | `1.97 MiB` | `50` matches |
| `pread-parallel` | `141.01 ms` | `12.57 ms` | `7,262.11 MiB/s` | `151.05 ms` | `9.84 ms` | `6,779.28 MiB/s` | `5.94–6.44 MiB` | `50` matches (~88x faster than `jq`) |
| `mmap-single` | `901.19 ms` | `14.17 ms` | `1,136.28 MiB/s` | `882.64 ms` | `14.90 ms` | `1,160.15 MiB/s` | `1,025.70 MiB` | `50` matches |
| `mmap-parallel` | `136.09 ms` | `13.13 ms` | `7,524.53 MiB/s` | `136.36 ms` | `11.16 ms` | `7,509.55 MiB/s` | `1,025.98–1,026.02 MiB` | `50` matches |
| `jq -R fromjson?` | `12,450.00 ms` | — | `82.25 MiB/s` | — | — | — | `2.36 MiB` | `/usr/bin/time -l` (`12.45 s` real) |

### 5 GiB (`5,368,709,150` bytes, `27,514,116` lines)

| Mode | Pass 1 Duration | Pass 1 TTFM | Pass 1 Throughput | Pass 2 Duration | Pass 2 TTFM | Pass 2 Throughput | Peak RSS | Notes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `index` | `1,793.68 ms` | `N/A` | `2,854.46 MiB/s` | `1,599.59 ms` | `N/A` | `3,200.83 MiB/s` | `31.69–33.30 MiB` | `stride=16`, `index_mem=13.12 MiB` |
| `pread-single` | `4,564.53 ms` | `45.93 ms` | `1,121.69 MiB/s` | `4,794.07 ms` | `46.66 ms` | `1,067.99 MiB/s` | `1.97 MiB` | `50` matches |
| `pread-parallel` | `1,942.12 ms` | `124.34 ms` | `2,636.30 MiB/s` | `1,856.33 ms` | `125.71 ms` | `2,758.13 MiB/s` | `5.61–6.95 MiB` | `50` matches |
| `mmap-single` | `12,140.20 ms` | `902.50 ms` | `421.74 MiB/s` | `11,429.03 ms` | `132.04 ms` | `447.98 MiB/s` | `2,892.20–2,915.16 MiB` | `50` matches |
| `mmap-parallel` | `5,610.86 ms` | `159.51 ms` | `912.52 MiB/s` | `6,153.18 ms` | `176.41 ms` | `832.09 MiB/s` | `3,894.80–3,913.55 MiB` | `50` matches |

---

## Key Engineering Takeaways (`pread` vs. `mmap`)

1. **Bounded Peak RSS (`~6 MiB` vs. `~3.9 GiB`):** On the 5 GiB dataset, `pread-parallel` stayed under **7 MiB Peak RSS** (`1.97 MiB` single-threaded), whereas `mmap-parallel` faulted **3.89–3.91 GiB** into the process's resident set.
2. **3x Faster at 5 GiB Scale:** When file pages fit comfortably in RAM (100 MiB and 1 GiB), `pread-parallel` and `mmap-parallel` perform within ~4% of each other (~6.0–7.5 GiB/s). Once the dataset reaches 5 GiB on a 16 GiB machine, page-fault and VM pressure degrade `mmap-parallel` to **832–913 MiB/s** (`5.6–6.2 s`), while chunked `pread-parallel` sustains **2,636–2,758 MiB/s** (`1.86–1.94 s`).
3. **`SIGBUS` Safety:** `mmap` can crash the process with `SIGBUS` if an active log file is truncated externally while mapped. `pread` returns clean short reads / EOF without risking process termination, confirming `pread` as the right default backend.

---

## macOS Desktop App Profiling (`Instruments / xctrace`)

Reproducible measurement script: [`./macos/scripts/profile_app.sh`](./macos/scripts/profile_app.sh) (`./build/release/tools/logloom-gen --size-mb 1024 --seed 42`, `LogloomApp` release bundle `v1.0.0`, `xctrace version 27.2 (27B52)` on macOS `27.2.1`).

| Dataset | Measurement | Result | Notes |
| --- | --- | ---: | --- |
| `1 GiB` (`5,502,862` lines) | `Time Profiler` (`6s` launch + background index) | `222` samples / `0` potential hangs | Instant `500`-line head render + background `LineIndex` build |
| `1 GiB` (`5,502,862` lines) | `Animation Hitches` (`5s` launch) | `2` cold-launch hitches | Initial AppKit/SwiftUI window creation only |
| `1 GiB` (`5,502,862` lines) | Peak RSS (`24` samples across `6s`) | `96.45 MiB` (`98,768 KiB`) | Bounded `LinePageCache` (`32 × 256` rows) + strided index |

---

## Real-World Dataset Benchmark (`Loghub Android_v1`)

To complement synthetic JSONL tests, Logloom was benchmarked against the real-world **Android_v1** framework log from [logpai/loghub](https://github.com/logpai/loghub) ([Zenodo archive](https://zenodo.org/records/8196385/files/Android_v1.zip?download=1)):

- **Dataset:** `Android.log` — `192,270,829` bytes (`183.36 MiB`), `1,555,005` plain-text lines (`0` malformed/oversized)
- **Queries tested:**
  - Substring benchmark via `logloom-bench --query "ActivityManager"` (`67,983` matches)
  - Regex CLI search via `logloom search --query "Exception|Error|FATAL" --regex`

| Mode | Pass 1 Duration | Pass 2 Duration | Throughput | Peak RSS | Notes |
| --- | ---: | ---: | ---: | ---: | --- |
| `index` (`LineIndex::build`) | `39.55 ms` | `39.43 ms` | `4,636.70–4,650.50 MiB/s` | `29.91 MiB` | `stride=1`, `index_mem=11.86 MiB` |
| `pread-parallel` (`ActivityManager`) | `13.88 ms` | `12.46 ms` | `13,214.37–14,718.26 MiB/s` | `13.08–13.38 MiB` | `67,983` matches, TTFM `3.07–4.27 ms` |
| `mmap-parallel` (`ActivityManager`) | `14.21 ms` | `14.16 ms` | `12,903.36–12,953.50 MiB/s` | `193.42–194.03 MiB` | `67,983` matches, TTFM `3.13–3.20 ms` |
| `logloom search --regex --limit 100` | `810 ms` | — | — | `6.73 MiB` | First 100 matches for `Exception\|Error\|FATAL` |
| `logloom search --regex` (full scan) | `4,130 ms` | — | — | `10.81 MiB` | Full-file regex scan (`std::regex`) |
| `Logloom.app` (`open` + full index) | `< 1s` usable | — | — | `157.78 MiB` (`161,568 KiB`) | Sampled over `4s` (`ps -o rss=`), 2-column plain-text mode |

### Reproduce on `Android_v1`

```bash
mkdir -p /tmp/loghub_android && cd /tmp/loghub_android
curl -fL "https://zenodo.org/records/8196385/files/Android_v1.zip?download=1" -o Android_v1.zip
unzip Android_v1.zip

/path/to/logloom/build/release/cli/logloom stats Android.log
/path/to/logloom/build/release/tools/logloom-bench --file Android.log --mode index
/path/to/logloom/build/release/tools/logloom-bench --file Android.log --mode pread-parallel --query "ActivityManager"
/usr/bin/time -l /path/to/logloom/build/release/cli/logloom search Android.log --query "Exception|Error|FATAL" --regex --limit 100 > /dev/null
```
