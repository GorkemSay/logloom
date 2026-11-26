#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/docs"
MOV_PATH="/tmp/logloom_demo.mov"
GIF_PATH="${OUT_DIR}/demo.gif"

mkdir -p "${OUT_DIR}"
echo "Select the Logloom window when prompted to capture a 10-second demo video..."
screencapture -v -T 10 "${MOV_PATH}"

if command -v ffmpeg >/dev/null 2>&1; then
  ffmpeg -y -i "${MOV_PATH}" \
    -vf "fps=15,scale=1120:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse" \
    "${GIF_PATH}"
  rm -f "${MOV_PATH}"
  echo "Created ${GIF_PATH}"
else
  echo "ffmpeg not installed; raw capture saved to ${MOV_PATH}"
fi
