#!/usr/bin/env bash
# Serial monitor for an IDF app directory. Usage: APP=ir_bridge ./scripts/monitor.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$ROOT/.." && pwd)"
APP="${APP:-${1:-}}"
if [[ -z "$APP" ]]; then
  echo "usage: APP=<name> $0   or   $0 <name>" >&2
  exit 1
fi
APP_DIR="$ROOT/apps/$APP"
if [[ ! -d "$APP_DIR" ]]; then
  echo "error: unknown app '$APP'" >&2
  exit 1
fi

PORT="${PORT:-$("$REPO/scripts/detect-esp-port.sh")}"
echo "Using PORT=$PORT app=$APP"

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

cd "$APP_DIR"
idf.py -p "$PORT" monitor
