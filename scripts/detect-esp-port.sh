#!/usr/bin/env bash
# Detect a serial port for the ESP32 USB-UART bridge.
#
# Usage:
#   ./scripts/detect-esp-port.sh           # print path, exit 0/1
#   ./scripts/detect-esp-port.sh --verbose # also print match reason on stderr
#   PORT=$(./scripts/detect-esp-port.sh)
#
# Override: if PORT is already set and exists, use it as-is.
# Preference order:
#   1. Stable /dev/serial/by-id/* for known ESP-ish USB-UART chips
#   2. udev properties on ttyUSB* / ttyACM* (vendor/driver match)
#   3. Single remaining USB serial if exactly one is present
#
# Known USB-UART families (vendor id or driver name):
#   Silicon Labs CP210x  (10c4)  — this project's board
#   WCH CH340/CH341      (1a86)
#   FTDI                 (0403)
#   Espressif native USB (303a)  — S2/S3/C3 style
#   Prolific PL2303      (067b)

set -euo pipefail

VERBOSE=0
for arg in "$@"; do
  case "$arg" in
    -v|--verbose) VERBOSE=1 ;;
    -h|--help)
      sed -n '2,25p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
  esac
done

log() {
  if [[ "$VERBOSE" -eq 1 ]]; then
    printf '%s\n' "$*" >&2
  fi
}

# Honor explicit PORT if usable
if [[ -n "${PORT:-}" && -e "${PORT}" && -r "${PORT}" && -w "${PORT}" ]]; then
  log "using PORT from environment: $PORT"
  printf '%s\n' "$PORT"
  exit 0
fi

# Match by-id basenames or udev vendor/driver
matches_esp_uart() {
  local s
  s=$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')
  case "$s" in
    *cp210*|*silicon_labs*|*10c4*) return 0 ;;
    *ch340*|*ch341*|*1a86*|*qin_heng*|*wch*) return 0 ;;
    *ftdi*|*0403*|*ft232*) return 0 ;;
    *303a*|*espressif*) return 0 ;;
    *pl2303*|*067b*|*prolific*) return 0 ;;
    *usb*serial*|*usb-serial*) return 0 ;;
  esac
  return 1
}

resolve_real() {
  # Prefer canonical /dev/ttyUSBn for tooling; keep by-id if readlink fails
  local p="$1"
  if [[ -L "$p" ]]; then
    readlink -f "$p" 2>/dev/null || printf '%s\n' "$p"
  else
    printf '%s\n' "$p"
  fi
}

candidates=()

# 1) Stable by-id symlinks
if [[ -d /dev/serial/by-id ]]; then
  shopt -s nullglob
  for link in /dev/serial/by-id/*; do
    base=$(basename "$link")
    if matches_esp_uart "$base"; then
      real=$(resolve_real "$link")
      if [[ -e "$real" && -r "$real" && -w "$real" ]]; then
        log "by-id match: $base -> $real"
        candidates+=("$real")
      else
        log "by-id match but not rw: $real (add user to uucp/dialout?)"
      fi
    fi
  done
  shopt -u nullglob
fi

# 2) udev scan of ttyUSB* / ttyACM*
if [[ ${#candidates[@]} -eq 0 ]]; then
  shopt -s nullglob
  for node in /dev/ttyUSB* /dev/ttyACM*; do
    [[ -e "$node" ]] || continue
    props=""
    if command -v udevadm >/dev/null 2>&1; then
      props=$(udevadm info -q property -n "$node" 2>/dev/null || true)
    fi
    blob="$node $props"
    if matches_esp_uart "$blob"; then
      if [[ -r "$node" && -w "$node" ]]; then
        log "udev match: $node"
        candidates+=("$node")
      else
        log "udev match but not rw: $node"
      fi
    fi
  done
  shopt -u nullglob
fi

# Deduplicate while preserving order
unique=()
for c in "${candidates[@]+"${candidates[@]}"}"; do
  skip=0
  for u in "${unique[@]+"${unique[@]}"}"; do
    [[ "$u" == "$c" ]] && skip=1 && break
  done
  [[ $skip -eq 0 ]] && unique+=("$c")
done
candidates=("${unique[@]+"${unique[@]}"}")

if [[ ${#candidates[@]} -eq 1 ]]; then
  printf '%s\n' "${candidates[0]}"
  exit 0
fi

if [[ ${#candidates[@]} -gt 1 ]]; then
  echo "error: multiple ESP-ish serial ports found:" >&2
  for c in "${candidates[@]}"; do
    echo "  $c" >&2
  done
  echo "set PORT=/dev/tty… explicitly" >&2
  exit 2
fi

# 3) Exactly one USB serial device total
usb_serials=()
shopt -s nullglob
for node in /dev/ttyUSB* /dev/ttyACM*; do
  [[ -e "$node" && -r "$node" && -w "$node" ]] && usb_serials+=("$node")
done
shopt -u nullglob

if [[ ${#usb_serials[@]} -eq 1 ]]; then
  log "single USB serial fallback: ${usb_serials[0]}"
  printf '%s\n' "${usb_serials[0]}"
  exit 0
fi

if [[ ${#usb_serials[@]} -gt 1 ]]; then
  echo "error: no known ESP USB-UART match; multiple serial ports:" >&2
  for c in "${usb_serials[@]}"; do
    echo "  $c" >&2
  done
  echo "set PORT=/dev/tty… or plug only the ESP board" >&2
  exit 2
fi

echo "error: no usable ESP serial port found" >&2
echo "  check USB cable/data; groups (uucp/dialout); ls /dev/serial/by-id/" >&2
exit 1
