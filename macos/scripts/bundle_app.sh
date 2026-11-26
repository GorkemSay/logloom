#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MACOS_DIR="${ROOT_DIR}/macos"
LIB_PATH="${ROOT_DIR}/build/release/core/liblogloom.a"
APP_OUT_DIR="${ROOT_DIR}/build/macos"
APP_BUNDLE="${APP_OUT_DIR}/Logloom.app"
SIGN_IDENTITY="${SIGN_IDENTITY:--}"

if [[ ! -f "${LIB_PATH}" ]]; then
  echo "Building liblogloom (release preset)..."
  cmake --preset release -S "${ROOT_DIR}"
  cmake --build --preset release --target logloom_core
fi

echo "Building LogloomApp (SwiftPM release)..."
swift build --package-path "${MACOS_DIR}" -c release --product LogloomApp

BIN_DIR="$(swift build --package-path "${MACOS_DIR}" -c release --show-bin-path)"
BIN_PATH="${BIN_DIR}/LogloomApp"

rm -rf "${APP_BUNDLE}"
mkdir -p "${APP_BUNDLE}/Contents/MacOS" "${APP_BUNDLE}/Contents/Resources"
cp "${BIN_PATH}" "${APP_BUNDLE}/Contents/MacOS/LogloomApp"
cp "${MACOS_DIR}/Resources/Info.plist" "${APP_BUNDLE}/Contents/Info.plist"

echo "Signing Logloom.app with App Sandbox entitlements (identity: ${SIGN_IDENTITY})..."
codesign --force --sign "${SIGN_IDENTITY}" \
  --entitlements "${MACOS_DIR}/Resources/Logloom.entitlements" \
  "${APP_BUNDLE}"

codesign --verify --verbose=2 "${APP_BUNDLE}"
echo "Created signed bundle: ${APP_BUNDLE}"
