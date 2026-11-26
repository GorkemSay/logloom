#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/release}"
DATA_DIR="${DATA_DIR:-${ROOT_DIR}/bench_data}"
TRACE_ID="${TRACE_ID:-req-target-001}"

GEN_BIN="${BUILD_DIR}/tools/logloom-gen"
BENCH_BIN="${BUILD_DIR}/tools/logloom-bench"

if [[ ! -x "${GEN_BIN}" || ! -x "${BENCH_BIN}" ]]; then
  echo "Building release preset..."
  cmake --build --preset release
fi

mkdir -p "${DATA_DIR}"

run_suite() {
  local label="$1"
  local size_mb="$2"
  local file="${DATA_DIR}/bench_${label}.jsonl"

  echo "=== Generating ${label} (${size_mb} MiB) ==="
  "${GEN_BIN}" --out "${file}" --size-mb "${size_mb}" --seed 42 --target-trace "${TRACE_ID}" --trace-hits 50

  for pass in 1 2; do
    echo "--- ${label} Pass ${pass} ---"
    for mode in index pread-single pread-parallel mmap-single mmap-parallel; do
      "${BENCH_BIN}" --file "${file}" --mode "${mode}" --trace-id "${TRACE_ID}"
    done
  done

  if command -v rg >/dev/null 2>&1; then
    echo "--- ${label} rg baseline ---"
    /usr/bin/time -l rg -c "\"request_id\":\"${TRACE_ID}\"" "${file}" 2>&1 || true
  fi

  if [[ "${size_mb}" -le 1024 ]] && command -v jq >/dev/null 2>&1; then
    echo "--- ${label} jq baseline ---"
    /usr/bin/time -l jq -R -c "fromjson? | select(.request_id == \"${TRACE_ID}\")" "${file}" >/dev/null || true
  fi
}

run_suite "100mb" 100
run_suite "1gb" 1024
run_suite "5gb" 5120
