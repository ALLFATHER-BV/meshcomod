#!/usr/bin/env bash
# meshcomod_boot — full-image restore / install (computer-side).
#
# Flashes a full 16 MB image to a T-Deck over USB. Use it to:
#   - restore a complete backup (BKUP*.IMG made by the recovery app), or
#   - install any stock full-flash firmware image (e.g. a Meshtastic/Meshcomod
#     factory image) to switch firmware.
#
# Why a computer: a full-chip restore overwrites the partition the recovery app
# runs from, which isn't possible on-device (see docs/ARCHITECTURE.md). On-device,
# use the recovery menu's slot-based switch instead. This script is the ultimate
# "un-brick / exact-clone" path and always works.
#
# The device must be flashable: with a good USB cable esptool auto-resets; if it
# can't connect, hold the trackball and power-cycle into download mode, then retry.
#
# Usage:
#   bash restore.sh <image.img> <serial-port>
#   bash restore.sh "/Volumes/NO NAME/BKUP0006.IMG" /dev/cu.usbmodem101
set -euo pipefail

IMG="${1:?usage: restore.sh <image.img> <serial-port>}"
PORT="${2:?usage: restore.sh <image.img> <serial-port>}"
[ -f "$IMG" ] || { echo "No such image: $IMG"; exit 1; }

SZ=$(stat -f%z "$IMG" 2>/dev/null || stat -c%s "$IMG" 2>/dev/null || echo 0)
echo ">> Restore $IMG ($SZ bytes) -> $PORT"
[ "$SZ" = "16777216" ] || echo "   NOTE: image is not exactly 16 MB; flashing at 0x0 anyway."

# If a sidecar manifest exists, verify the image hash before flashing.
MAN="${IMG%.IMG}.TXT"; [ -f "$MAN" ] || MAN="${IMG%.img}.txt"
if [ -f "$MAN" ]; then
    WANT=$(grep -i '^sha256=' "$MAN" | cut -d= -f2 | tr -dc '0-9a-fA-F')
    GOT=$(shasum -a 256 "$IMG" | awk '{print $1}')
    if [ -n "$WANT" ] && [ "$WANT" = "$GOT" ]; then echo "   manifest sha256 OK";
    elif [ -n "$WANT" ]; then echo "   !! sha256 MISMATCH (manifest=$WANT got=$GOT) — aborting"; exit 1; fi
fi

ESPTOOL=$(ls /Users/kaj/.platformio/packages/tool-esptoolpy*/esptool.py 2>/dev/null | head -1)
[ -n "$ESPTOOL" ] || { echo "esptool not found under ~/.platformio"; exit 1; }

echo ">> Writing full image at 0x0 (rewrites the ENTIRE chip)..."
python3 "$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 921600 \
    write_flash --flash_size 16MB 0x0 "$IMG"
echo ">> Done — the device now holds the restored image. Power-cycle if needed."
