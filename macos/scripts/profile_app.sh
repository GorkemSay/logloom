#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP_BIN="${ROOT_DIR}/build/macos/Logloom.app/Contents/MacOS/LogloomApp"
GEN_BIN="${ROOT_DIR}/build/release/tools/logloom-gen"

if [[ ! -x "${APP_BIN}" ]]; then
  echo "Missing ${APP_BIN}; run ./macos/scripts/bundle_app.sh first." >&2
  exit 1
fi
if [[ ! -x "${GEN_BIN}" ]]; then
  echo "Missing ${GEN_BIN}; build the release preset first." >&2
  exit 1
fi

CUSTOM_DATASET="${1:-}"
TMP_DATASET="/tmp/logloom_profile_1gb.jsonl"
TIME_TRACE="/tmp/logloom_time_profiler.trace"
HITCH_TRACE="/tmp/logloom_animation_hitches.trace"
TOC_XML="/tmp/logloom_time_toc.xml"

TIME_XML="/tmp/logloom_time_profile.xml"

cleanup() {
  rm -rf "${TMP_DATASET}" "${TIME_TRACE}" "${HITCH_TRACE}" "${TOC_XML}" "${TIME_XML}"
}
trap cleanup EXIT

if [[ -n "${CUSTOM_DATASET}" ]]; then
  DATASET="${CUSTOM_DATASET}"
else
  DATASET="${TMP_DATASET}"
  echo "Generating 1 GiB deterministic dataset at ${DATASET}..."
  "${GEN_BIN}" --size-mb 1024 --seed 42 --out "${DATASET}"
fi

DATASET_BYTES="$(stat -f%z "${DATASET}")"
echo "Dataset size: ${DATASET_BYTES} bytes"

echo ""
echo "=== 1. Instruments Time Profiler (6s launch + 1 GiB open/index) ==="
rm -rf "${TIME_TRACE}" "${TOC_XML}" "${TIME_XML}"
REC_RC=0
xcrun xctrace record \
  --template 'Time Profiler' \
  --time-limit 6s \
  --output "${TIME_TRACE}" \
  --launch -- "${APP_BIN}" "${DATASET}" || REC_RC=$?
if [[ ! -d "${TIME_TRACE}" ]]; then
  echo "Time Profiler trace was not created (exit ${REC_RC})." >&2
  exit 1
fi
echo "Time Profiler recorded (xctrace exit ${REC_RC}, trace exists)."

xcrun xctrace export --input "${TIME_TRACE}" --toc > "${TOC_XML}"
echo "Time Profiler TOC schemas:"
python3 - "${TOC_XML}" <<'PY'
import sys, xml.etree.ElementTree as ET
tree = ET.parse(sys.argv[1])
schemas = sorted({el.attrib.get("schema", "") for el in tree.findall(".//table") if el.attrib.get("schema")})
print(" ", ", ".join(schemas))
PY

if xcrun xctrace export --input "${TIME_TRACE}" \
  --xpath '/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]' > "${TIME_XML}" 2>/dev/null; then
  python3 - "${TIME_XML}" <<'PY'
import collections, sys, xml.etree.ElementTree as ET
tree = ET.parse(sys.argv[1])
rows = tree.findall(".//row")
frames_by_id = {}
for el in tree.findall(".//frame"):
    fid = el.attrib.get("id")
    if fid and "name" in el.attrib:
        frames_by_id[fid] = el.attrib["name"]
top_frames = collections.Counter()
for row in rows:
    first_frame = row.find(".//frame")
    if first_frame is not None:
        name = first_frame.attrib.get("name") or frames_by_id.get(first_frame.attrib.get("ref", ""))
        if name:
            top_frames[name] += 1
print(f"Time Profiler rows (samples): {len(rows)}")
if top_frames:
    print("Top leaf frames:")
    for frame_name, count in top_frames.most_common(8):
        print(f"  {count:4d}  {frame_name}")
PY
fi

if xcrun xctrace export --input "${TIME_TRACE}" \
  --xpath '/trace-toc/run[@number="1"]/data/table[@schema="potential-hangs"]' > "${TIME_XML}" 2>/dev/null; then
  python3 - "${TIME_XML}" <<'PY'
import sys, xml.etree.ElementTree as ET
tree = ET.parse(sys.argv[1])
print(f"Potential hangs recorded: {len(tree.findall('.//row'))}")
PY
fi

echo ""
echo "=== 2. Instruments Animation Hitches (5s check) ==="
rm -rf "${HITCH_TRACE}"
HITCH_RC=0
xcrun xctrace record \
  --template 'Animation Hitches' \
  --time-limit 5s \
  --output "${HITCH_TRACE}" \
  --launch -- "${APP_BIN}" "${DATASET}" || HITCH_RC=$?
if [[ -d "${HITCH_TRACE}" ]]; then
  echo "Animation Hitches recorded (xctrace exit ${HITCH_RC}). Schemas:"
  xcrun xctrace export --input "${HITCH_TRACE}" --toc | grep -Eo 'schema="[^"]+"' | sort -u || true
  if xcrun xctrace export --input "${HITCH_TRACE}" \
    --xpath '/trace-toc/run[@number="1"]/data/table[@schema="hitches"]' > "${TIME_XML}" 2>/dev/null; then
    python3 - "${TIME_XML}" <<'PY'
import sys, xml.etree.ElementTree as ET
tree = ET.parse(sys.argv[1])
print(f"Animation hitches recorded: {len(tree.findall('.//row'))}")
PY
  fi
else
  echo "Animation Hitches template unavailable or exited ${HITCH_RC}."
fi

echo ""
echo "=== 3. Peak RSS Sampling (6s over 1 GiB launch + background index) ==="
"${APP_BIN}" "${DATASET}" >/dev/null 2>&1 &
APP_PID=$!
PEAK_KB=0
SAMPLES=0
for _ in $(seq 1 24); do
  if ! kill -0 "${APP_PID}" 2>/dev/null; then
    break
  fi
  RSS_KB="$(ps -o rss= -p "${APP_PID}" 2>/dev/null | tr -d ' ' || true)"
  if [[ -n "${RSS_KB}" && "${RSS_KB}" =~ ^[0-9]+$ ]]; then
    SAMPLES=$((SAMPLES + 1))
    if (( RSS_KB > PEAK_KB )); then
      PEAK_KB="${RSS_KB}"
    fi
  fi
  sleep 0.25
done
kill "${APP_PID}" 2>/dev/null || true
wait "${APP_PID}" 2>/dev/null || true

PEAK_MIB="$(awk "BEGIN { printf \"%.2f\", ${PEAK_KB} / 1024.0 }")"
echo "Samples collected: ${SAMPLES}, Peak RSS: ${PEAK_KB} KiB (${PEAK_MIB} MiB)"
