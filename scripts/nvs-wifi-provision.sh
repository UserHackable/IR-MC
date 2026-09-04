#!/usr/bin/env bash
# Write Wi‑Fi (and optional MQTT) into the ESP32 NVS partition.
#
# Reads the *first* entry from secrets/wifi.yaml:
#   ---
#   - ssid: Ferney
#     password: "secret"
#
# If secrets/mqtt.yaml exists, its keys are written into the same NVS image
# (namespace "mqtt") so a later wifi-only flash does not wipe the broker URI.
#
# Usage (repo root):
#   ./scripts/nvs-wifi-provision.sh
#   ./scripts/nvs-wifi-provision.sh /path/to/wifi.yaml
#
# Optional static identity (wifi.yaml is shared across boards, so these
# are env vars, not YAML keys):
#   WIFI_HOSTNAME=ir22 WIFI_IP=192.168.72.22 ./scripts/fw idf nvs-wifi
#   WIFI_GATEWAY / WIFI_NETMASK default to 192.168.72.1 / 255.255.255.0
#   ESPTOOL_BAUD defaults to 460800; CP2102 boards often need 115200.
#
# Requires: ESP-IDF installed (./scripts/fw idf install), board on USB, secrets file.
# Artifacts under secrets/generated/ (gitignored).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
YAML="${1:-$REPO/secrets/wifi.yaml}"
MQTT_YAML="${MQTT_YAML:-$REPO/secrets/mqtt.yaml}"
GEN_DIR="$REPO/secrets/generated"
CSV="$GEN_DIR/wifi_nvs.csv"
BIN="$GEN_DIR/wifi_nvs.bin"
# Default single-app partition table: NVS at 0x9000, size 0x6000
NVS_OFFSET="${NVS_OFFSET:-0x9000}"
NVS_SIZE="${NVS_SIZE:-0x6000}"

IDF_PATH_DEFAULT="${IR_MC_IDF_PATH:-${HOME}/esp/esp-idf}"
export IDF_PATH="${IDF_PATH:-$IDF_PATH_DEFAULT}"

if [[ ! -f "$YAML" ]]; then
  echo "error: missing $YAML" >&2
  echo "  cp secrets/wifi.yaml.example secrets/wifi.yaml  # edit ssid/password" >&2
  exit 1
fi

if [[ ! -f "$IDF_PATH/export.sh" ]]; then
  echo "error: ESP-IDF not found at IDF_PATH=$IDF_PATH" >&2
  echo "  ./scripts/fw idf install" >&2
  exit 1
fi

# shellcheck disable=SC1091
source "$IDF_PATH/export.sh" >/dev/null

GEN_PY="$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
if [[ ! -f "$GEN_PY" ]]; then
  echo "error: nvs_partition_gen.py not found under IDF" >&2
  exit 1
fi

mkdir -p "$GEN_DIR"

# Parse first network from simple YAML (stdlib Python)
eval "$(python3 - "$YAML" <<'PY'
import re, sys, shlex
from pathlib import Path
text = Path(sys.argv[1]).read_text(encoding="utf-8")
lines = [ln for ln in text.splitlines() if not re.match(r"^\s*#", ln)]
ssid = password = None
for ln in lines:
    m = re.match(r"^\s*-\s*ssid:\s*(.*)$", ln)
    if m:
        if ssid is not None:
            break  # first network only
        ssid = m.group(1).strip().strip("\"'")
        continue
    m = re.match(r"^\s*password:\s*(.*)$", ln)
    if m and ssid is not None and password is None:
        raw = m.group(1).strip()
        if (raw.startswith('"') and raw.endswith('"')) or (raw.startswith("'") and raw.endswith("'")):
            raw = raw[1:-1]
        password = raw
if not ssid:
    sys.stderr.write("error: no ssid in yaml\n")
    sys.exit(1)
if password is None:
    password = ""
print(f"SSID={shlex.quote(ssid)}")
print(f"PASSWORD={shlex.quote(password)}")
PY
)"

MQTT_URI=""
MQTT_USER=""
MQTT_PASS=""
MQTT_PREFIX="ir"
if [[ -f "$MQTT_YAML" ]]; then
  eval "$(python3 - "$MQTT_YAML" <<'PY'
import re, sys, shlex
from pathlib import Path
text = Path(sys.argv[1]).read_text(encoding="utf-8")
vals = {"uri": "", "username": "", "password": "", "prefix": "ir"}
for ln in text.splitlines():
    if re.match(r"^\s*#", ln) or not ln.strip():
        continue
    m = re.match(r"^\s*(uri|username|password|prefix):\s*(.*)$", ln)
    if not m:
        continue
    raw = m.group(2).strip()
    if (raw.startswith('"') and raw.endswith('"')) or (raw.startswith("'") and raw.endswith("'")):
        raw = raw[1:-1]
    vals[m.group(1)] = raw
print(f"MQTT_URI={shlex.quote(vals['uri'])}")
print(f"MQTT_USER={shlex.quote(vals['username'])}")
print(f"MQTT_PASS={shlex.quote(vals['password'])}")
print(f"MQTT_PREFIX={shlex.quote(vals['prefix'] or 'ir')}")
PY
)"
fi

csv_ok() {
  if [[ "$1" == *","* ]]; then
    echo "error: NVS CSV values must not contain commas: $2" >&2
    exit 1
  fi
}
WIFI_HOSTNAME="${WIFI_HOSTNAME:-}"
WIFI_IP="${WIFI_IP:-}"
WIFI_GATEWAY="${WIFI_GATEWAY:-}"
WIFI_NETMASK="${WIFI_NETMASK:-}"
ESPTOOL_BAUD="${ESPTOOL_BAUD:-460800}"
if [[ -n "$WIFI_IP" && -z "$WIFI_GATEWAY" ]]; then
  WIFI_GATEWAY="192.168.72.1"
fi
if [[ -n "$WIFI_IP" && -z "$WIFI_NETMASK" ]]; then
  WIFI_NETMASK="255.255.255.0"
fi

csv_ok "$SSID" ssid
csv_ok "$PASSWORD" password
csv_ok "$MQTT_URI" mqtt.uri
csv_ok "$MQTT_USER" mqtt.username
csv_ok "$MQTT_PASS" mqtt.password
csv_ok "$MQTT_PREFIX" mqtt.prefix
csv_ok "$WIFI_HOSTNAME" hostname
csv_ok "$WIFI_IP" ip
csv_ok "$WIFI_GATEWAY" gateway
csv_ok "$WIFI_NETMASK" netmask

{
  echo "key,type,encoding,value"
  echo "wifi,namespace,,"
  echo "ssid,data,string,${SSID}"
  echo "password,data,string,${PASSWORD}"
  if [[ -n "$WIFI_HOSTNAME" ]]; then
    echo "hostname,data,string,${WIFI_HOSTNAME}"
  fi
  if [[ -n "$WIFI_IP" ]]; then
    echo "ip,data,string,${WIFI_IP}"
  fi
  if [[ -n "$WIFI_GATEWAY" ]]; then
    echo "gateway,data,string,${WIFI_GATEWAY}"
  fi
  if [[ -n "$WIFI_NETMASK" ]]; then
    echo "netmask,data,string,${WIFI_NETMASK}"
  fi
  if [[ -n "$MQTT_URI" ]]; then
    echo "mqtt,namespace,,"
    echo "uri,data,string,${MQTT_URI}"
    echo "username,data,string,${MQTT_USER}"
    echo "password,data,string,${MQTT_PASS}"
    echo "prefix,data,string,${MQTT_PREFIX}"
  fi
} >"$CSV"

echo "Generating NVS image for SSID='${SSID}' (password not printed)"
if [[ -n "$WIFI_HOSTNAME" || -n "$WIFI_IP" ]]; then
  echo "Including hostname='${WIFI_HOSTNAME:-}' ip='${WIFI_IP:-}'"
fi
if [[ -n "$MQTT_URI" ]]; then
  echo "Including MQTT uri='${MQTT_URI}' prefix='${MQTT_PREFIX}'"
fi
python3 "$GEN_PY" generate "$CSV" "$BIN" "$NVS_SIZE"

PORT="${PORT:-$("$REPO/scripts/detect-esp-port.sh")}"
echo "Flashing NVS -> ${PORT} @ ${NVS_OFFSET} (${BIN}) baud ${ESPTOOL_BAUD}"

python3 -m esptool --chip esp32 -p "$PORT" -b "$ESPTOOL_BAUD" \
  write_flash "$NVS_OFFSET" "$BIN"

echo "OK: NVS namespace 'wifi' provisioned (ssid + password)."
if [[ -n "$WIFI_HOSTNAME" || -n "$WIFI_IP" ]]; then
  echo "    hostname='${WIFI_HOSTNAME:-}' ip='${WIFI_IP:-}'"
fi
if [[ -n "$MQTT_URI" ]]; then
  echo "    NVS namespace 'mqtt' provisioned (uri + prefix)."
fi
echo "    Flash app:  ./scripts/fw idf upload ir_bridge"
echo "    Monitor:    ./scripts/fw idf monitor ir_bridge"
