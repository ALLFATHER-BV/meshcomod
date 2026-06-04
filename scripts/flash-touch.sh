#!/usr/bin/env bash
# Flash the heltec_v4 TFT touch firmware while preserving NVS (Wi-Fi creds etc).
#
# The default merged.bin pads gaps with 0xFF, which overwrites the NVS
# partition at 0x9000–0xe000 — i.e. clears saved Wi-Fi credentials on every
# flash. This script flashes the four components individually, leaving NVS
# alone.
#
# Usage: scripts/flash-touch.sh [/dev/cu.usbmodemXXXX]

set -euo pipefail

PORT="${1:-/dev/cu.usbmodem1101}"
BUILD_DIR="$(cd "$(dirname "$0")/.." && pwd)/.pio/build/heltec_v4_tft_companion_radio_usb_tcp_touch"
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy@1.40501.0/esptool.py"

for f in "$BUILD_DIR/bootloader.bin" "$BUILD_DIR/partitions.bin" \
         "$BUILD_DIR/firmware.bin"  "$BOOT_APP0"  "$ESPTOOL"; do
  [[ -f "$f" ]] || { echo "missing: $f"; exit 1; }
done

python3 "$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 460800 write_flash \
  0x0000 "$BUILD_DIR/bootloader.bin" \
  0x8000 "$BUILD_DIR/partitions.bin" \
  0xe000 "$BOOT_APP0" \
  0x10000 "$BUILD_DIR/firmware.bin"
