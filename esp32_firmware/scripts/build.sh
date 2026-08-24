#!/usr/bin/env bash
# Build one IDF app. Usage: APP=ir_bridge ./scripts/build.sh
# Prefer: ./scripts/fw idf build <app>
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="${APP:-${1:-}}"
IDF_TARGET="${IDF_TARGET:-esp32}"
if [[ -z "$APP" ]]; then
  echo "usage: APP=<name> $0   or   $0 <name>" >&2
  echo "apps:" >&2
  for d in "$ROOT/apps"/*/; do
    [[ -f "$d/CMakeLists.txt" ]] && echo "  $(basename "$d")" >&2
  done
  exit 1
fi
APP_DIR="$ROOT/apps/$APP"
if [[ ! -f "$APP_DIR/CMakeLists.txt" ]]; then
  echo "error: unknown app '$APP' (no apps/$APP/CMakeLists.txt)" >&2
  exit 1
fi

if command -v mise >/dev/null 2>&1; then
  (cd "$ROOT" && mise install >/dev/null)
  _cmake=$(cd "$ROOT" && mise which cmake 2>/dev/null || true)
  _ninja=$(cd "$ROOT" && mise which ninja 2>/dev/null || true)
  [[ -n "$_cmake" ]] && export PATH="$(dirname "$_cmake"):$PATH"
  [[ -n "$_ninja" ]] && export PATH="$(dirname "$_ninja"):$PATH"
  unset _cmake _ninja
fi

if ! command -v idf.py >/dev/null 2>&1; then
  # shellcheck disable=SC1091
  source "$ROOT/scripts/export-idf.sh"
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found (mise install in esp32_firmware/)" >&2
  exit 1
fi

cd "$APP_DIR"
if [[ ! -f sdkconfig ]]; then
  echo "==> set-target $IDF_TARGET (first time for $APP)"
  idf.py set-target "$IDF_TARGET"
fi
echo "==> build app=$APP target=$IDF_TARGET"
idf.py build
