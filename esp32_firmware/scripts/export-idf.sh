#!/usr/bin/env bash
# Source this file (do not execute):  source ./scripts/export-idf.sh
# Loads ESP-IDF into the current shell.

IDF_PATH_DEFAULT="${IR_MC_IDF_PATH:-${HOME}/esp/esp-idf}"
export IDF_PATH="${IDF_PATH:-$IDF_PATH_DEFAULT}"

if [[ ! -f "${IDF_PATH}/export.sh" ]]; then
  echo "ESP-IDF not found at IDF_PATH=${IDF_PATH}" >&2
  echo "Run: ./scripts/install-idf.sh" >&2
  return 1 2>/dev/null || exit 1
fi

# shellcheck disable=SC1091
. "${IDF_PATH}/export.sh"
echo "IDF_PATH=$IDF_PATH  ($(idf.py --version 2>/dev/null || echo idf ready))"
