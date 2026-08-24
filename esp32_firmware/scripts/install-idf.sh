#!/usr/bin/env bash
# Clone ESP-IDF and install the ESP32 toolchain (once per machine).
# Does not modify this git repo's tracked tree with IDF sources.
set -euo pipefail

IDF_VERSION="${IDF_VERSION:-v5.3.2}"
IDF_PATH_DEFAULT="${IR_MC_IDF_PATH:-${HOME}/esp/esp-idf}"
IDF_PATH="${IDF_PATH:-$IDF_PATH_DEFAULT}"
TARGET="${IDF_TARGET:-esp32}"

echo "ESP-IDF path:  $IDF_PATH"
echo "Version/tag:   $IDF_VERSION"
echo "Install tools: $TARGET"

if [[ -d "$IDF_PATH/.git" ]]; then
  echo "==> Existing clone found; fetching tags and checking out $IDF_VERSION"
  git -C "$IDF_PATH" fetch --depth 1 origin tag "$IDF_VERSION" 2>/dev/null \
    || git -C "$IDF_PATH" fetch origin tag "$IDF_VERSION" || true
  git -C "$IDF_PATH" checkout "$IDF_VERSION"
  git -C "$IDF_PATH" submodule update --init --recursive
else
  echo "==> Cloning Espressif esp-idf ($IDF_VERSION)"
  mkdir -p "$(dirname "$IDF_PATH")"
  git clone -b "$IDF_VERSION" --recursive https://github.com/espressif/esp-idf.git "$IDF_PATH"
fi

echo "==> install.sh $TARGET (toolchains + Python env; may take a while)"
cd "$IDF_PATH"
./install.sh "$TARGET"

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cat <<EOF

ESP-IDF installed at ${IDF_PATH}.

From the repo root:

  ${REPO_ROOT}/scripts/fw idf build ir_bridge
  ${REPO_ROOT}/scripts/fw idf upload ir_bridge
  ${REPO_ROOT}/scripts/fw idf monitor ir_bridge

Or in a shell with IDF on PATH:

  . ${IDF_PATH}/export.sh
  ./scripts/fw idf build ir_bridge
EOF
