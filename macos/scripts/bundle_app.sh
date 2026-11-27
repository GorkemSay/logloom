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

APPICON_SRC="${MACOS_DIR}/Resources/AppIcon.appiconset"
if [[ -d "${APPICON_SRC}" ]]; then
  ICONSET_TMP="$(mktemp -d)"
  ICONSET_DIR="${ICONSET_TMP}/AppIcon.iconset"
  mkdir -p "${ICONSET_DIR}"
  cp "${APPICON_SRC}/icon-16@1x.png" "${ICONSET_DIR}/icon_16x16.png"
  cp "${APPICON_SRC}/icon-16@2x.png" "${ICONSET_DIR}/icon_16x16@2x.png"
  cp "${APPICON_SRC}/icon-32@1x.png" "${ICONSET_DIR}/icon_32x32.png"
  cp "${APPICON_SRC}/icon-32@2x.png" "${ICONSET_DIR}/icon_32x32@2x.png"
  cp "${APPICON_SRC}/icon-128@1x.png" "${ICONSET_DIR}/icon_128x128.png"
  cp "${APPICON_SRC}/icon-128@2x.png" "${ICONSET_DIR}/icon_128x128@2x.png"
  cp "${APPICON_SRC}/icon-256@1x.png" "${ICONSET_DIR}/icon_256x256.png"
  cp "${APPICON_SRC}/icon-256@2x.png" "${ICONSET_DIR}/icon_256x256@2x.png"
  cp "${APPICON_SRC}/icon-512@1x.png" "${ICONSET_DIR}/icon_512x512.png"
  cp "${APPICON_SRC}/icon-512@2x.png" "${ICONSET_DIR}/icon_512x512@2x.png"
  iconutil -c icns "${ICONSET_DIR}" -o "${APP_BUNDLE}/Contents/Resources/AppIcon.icns"
  rm -rf "${ICONSET_TMP}"
fi

echo "Signing Logloom.app with App Sandbox entitlements (identity: ${SIGN_IDENTITY})..."
codesign --force --sign "${SIGN_IDENTITY}" \
  --entitlements "${MACOS_DIR}/Resources/Logloom.entitlements" \
  "${APP_BUNDLE}"

codesign --verify --verbose=2 "${APP_BUNDLE}"
echo "Created signed bundle: ${APP_BUNDLE}"
