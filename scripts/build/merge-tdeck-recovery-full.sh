#!/usr/bin/env bash
# Build the T-Deck "recovery model" full install image:
#   CUSTOM bootloader (bootloader_launcher) + dual-slot(A/B)+factory+bootsel table
#   + erased otadata + recovery app @ factory(0x10000) + meshcomod touch app @
#   ota_0(0x110000). The custom BL boots factory(recovery) by DEFAULT and an ota
#   slot only on the one-shot `bootsel` flag, so recovery-first survives ANY app
#   in a slot (meshcomod, Meshtastic, a crashing build) — the BL enforces it.
#
# Flash this ONCE at 0x0 (after `erase_flash`) to convert a device to the
# dual-slot + recovery model. After that:
#   - meshcomod updates  : in-firmware OTA (no SD), or esptool write_flash 0x110000 <touch firmware.bin>
#   - recovery updates   : esptool write_flash 0x10000 <recovery firmware.bin>
# Both preserve the other component + nvs/tiles/spiffs.
#
# Prereqs: build both envs first (from the repo root):
#   python3 -m platformio run -e LilyGo_TDeck_recovery
#   python3 -m platformio run -e LilyGo_TDeck_companion_radio_touch
set -euo pipefail
cd "$(dirname "$0")/../.."

REC=.pio/build/LilyGo_TDeck_recovery
TCH=.pio/build/LilyGo_TDeck_companion_radio_touch
BL=examples/meshcomod_boot/bootloader_launcher/.pio/build/bootloader/bootloader.bin
OUT="${1:-out/meshcomod_tdeck_recovery_full.bin}"

for f in "$BL" "$REC/partitions.bin" "$REC/firmware.bin" "$TCH/firmware.bin"; do
  [ -f "$f" ] || { echo "MISSING: $f  (build the custom BL + both envs first)"; exit 1; }
done

# otadata: ship it ERASED (0xFF). The custom bootloader boots factory(recovery)
# by default regardless; the recovery's countdown then launches the slot (defaults
# to ota_0 when otadata is blank) via the one-shot bootsel flag.
OTADATA=/tmp/mc_otadata_blank.bin
python3 -c "open('$OTADATA','wb').write(b'\xff'*0x2000)"

mkdir -p "$(dirname "$OUT")"
# partitions.bin comes from the RECOVERY env (it builds partitions_shared.csv = the A/B+factory
# table). The touch env's own partitions.bin is the standalone dual-OTA table — do NOT use it here.
python3 -m esptool --chip esp32s3 merge_bin -o "$OUT" --flash_size keep \
  0x0      "$BL" \
  0x8000   "$REC/partitions.bin" \
  0xe000   "$OTADATA" \
  0x10000  "$REC/firmware.bin" \
  0x110000 "$TCH/firmware.bin"

echo "Wrote $OUT ($(stat -f%z "$OUT" 2>/dev/null || stat -c%s "$OUT") bytes)"
