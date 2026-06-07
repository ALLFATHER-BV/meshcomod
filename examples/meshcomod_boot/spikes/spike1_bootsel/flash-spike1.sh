#!/usr/bin/env bash
# Flash Spike 1 to a T-Deck and populate BOTH app partitions (factory="MAIN" and
# test="RECOVERY") with the same binary, so we can see which one the bootloader
# selects.
#
# Usage:  bash flash-spike1.sh /dev/cu.usbmodemXXXX
# (find the port with:  ls /dev/cu.usbmodem*  )
#
# If the first flash fails with "Resource busy"/timeout, put the T-Deck in
# download mode (hold BOOT/trackball while tapping reset) and retry.
set -euo pipefail

PORT="${1:?usage: flash-spike1.sh <serial-port>}"
PIO="${PIO:-/Users/kaj/Library/Python/3.9/bin/pio}"
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

echo ">> [1/2] Build + flash bootloader, partition table, and factory app (MAIN)"
"$PIO" run -e spike1_tdeck -t upload --upload-port "$PORT"

BIN="$HERE/.pio/build/spike1_tdeck/firmware.bin"
echo ">> [2/2] Clone the same app into the 'test' partition (RECOVERY) @ 0x210000"
# Uses PlatformIO's bundled esptool. If 'pkg exec' is unavailable, run esptool.py
# directly: esptool.py --chip esp32s3 --port "$PORT" write_flash 0x210000 "$BIN"
"$PIO" pkg exec -- esptool.py --chip esp32s3 --port "$PORT" --baud 921600 \
    write_flash 0x210000 "$BIN"

cat <<EOF

>> Done. Watch the serial output:
     $PIO device monitor -p $PORT -b 115200

   Expected:
     - Normal reset                         -> "Booted partition: MAIN (factory)"
     - Reset, release, then hold trackball
       click (GPIO0) for >=5 seconds        -> "Booted partition: RECOVERY (test)"
EOF
