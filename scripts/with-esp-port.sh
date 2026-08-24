#!/usr/bin/env bash
# Resolve PORT then run a command. Usage:
#   ./scripts/with-esp-port.sh idf.py flash
#   ./scripts/with-esp-port.sh echo "port is \$PORT"
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
PORT="${PORT:-$("$REPO/scripts/detect-esp-port.sh")}"
export PORT
exec "$@"
