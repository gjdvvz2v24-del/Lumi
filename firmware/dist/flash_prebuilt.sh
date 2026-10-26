#!/usr/bin/env bash
set -euo pipefail
: "${1:?Podaj port USB urządzenia, np. /dev/cu.usbmodem1101}"
cd "$(dirname "$0")"
PYTHON_BIN="${PYTHON:-python3}"
echo "Lumi 2.11.1-diverse: Sol Ultrafast, losowe piosenki Eleven Music, chrapanie bez Wi-Fi. Aktualizacja aplikacji i promptfs; klucze i Wi-Fi pozostają w NVS."
shasum -a 256 -c SHA256SUMS.txt
check_dir=$(mktemp -d)
trap 'rm -rf "$check_dir"' EXIT
"$PYTHON_BIN" -m esptool --chip esp32s3 --port "$1" --baud 115200 read_flash 0x8000 0xc00 "$check_dir/partition-table.bin"
cmp partition-table.bin "$check_dir/partition-table.bin" || { echo "Niezgodna tablica partycji; przerwano przed zapisem." >&2; exit 1; }
"$PYTHON_BIN" -m esptool --chip esp32s3 --port "$1" --baud 115200 write_flash 0x10000 bober_s3r_openai.bin 0x7e0000 promptfs.bin
