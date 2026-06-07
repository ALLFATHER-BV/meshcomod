#!/usr/bin/env bash
# ONE-TIME fresh install of the T-Deck dual-slot + recovery image. This ERASES the
# whole chip (wipes nvs/Wi-Fi creds/tiles/spiffs) and writes the full image at 0x0
# — use it to convert a device to the new model, or to set up a blank board.
#
# After this, do NOT use it for updates (it wipes user data). Updates are:
#   - meshcomod : in-firmware OTA (Settings -> Install update over Wi-Fi, no SD), or
#                 esptool write_flash 0x110000 .pio/build/LilyGo_TDeck_companion_radio_touch/firmware.bin
#   - recovery  : esptool write_flash 0x10000  .pio/build/LilyGo_TDeck_recovery/firmware.bin
#
# Usage: scripts/flash-tdeck-recovery-full.sh /dev/cu.usbmodemXXXX [image.bin]
set -euo pipefail
cd "$(dirname "$0")/.."

PORT="${1:?usage: $0 <port> [image.bin]   (ls /dev/cu.usbmodem*)}"
IMG="${2:-out/meshcomod_tdeck_recovery_full.bin}"
[ -f "$IMG" ] || { echo "MISSING image: $IMG  (run scripts/build/merge-tdeck-recovery-full.sh)"; exit 1; }

echo ">> erase_flash on $PORT (this wipes ALL data on the device)"
python3 -m esptool --chip esp32s3 -p "$PORT" erase_flash
echo ">> write_flash 0x0 $IMG"
python3 -m esptool --chip esp32s3 -p "$PORT" write_flash 0x0 "$IMG"
echo ">> done — the board boots meshcomod (ota_0); hold trackball / Reboot-to-recovery for the recovery."
