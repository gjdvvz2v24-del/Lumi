#!/usr/bin/env bash
set -euo pipefail
PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  echo "Usage: ./flash.sh /dev/cu.usbmodemXXXX" >&2
  exit 2
fi
: "${IDF_PATH:?Run: source ~/esp/esp-idf/export.sh first}"
cd "$(dirname "$0")"
python3 normalize_timestamps.py
if [[ ! -f sdkconfig ]] || ! grep -q '^CONFIG_IDF_TARGET="esp32s3"$' sdkconfig; then
  python3 "$IDF_PATH/tools/idf.py" set-target esp32s3
fi
python3 "$IDF_PATH/tools/idf.py" build
python3 "$IDF_PATH/tools/idf.py" -p "$PORT" -b 115200 flash
python3 "$IDF_PATH/tools/idf.py" -p "$PORT" -b 115200 monitor
