#!/usr/bin/env bash
# Copy companion USB+TCP build outputs from out/ to prebuilt/ and prebuilt/releases/<version>.
# Run after building:
#   export FIRMWARE_VERSION=v1.14.0.1
#   sh build.sh build-firmware heltec_v4_companion_radio_usb_tcp
#   sh build.sh build-firmware Heltec_v3_companion_radio_usb_tcp
# Usage: sh scripts/copy-release-bins.sh <version>
# Example: sh scripts/copy-release-bins.sh v1.14.0.1
# Versioned drops live under prebuilt/releases/companion/<version>/.

set -e
VERSION="${1:?Usage: $0 <version> e.g. v1.14.0.1}"
RELDIR="prebuilt/releases/companion/${VERSION}"
mkdir -p "prebuilt" "$RELDIR"

copy_one() {
  local src="$1"
  local dest_name="$2"
  if [ -z "$src" ] || [ ! -f "$src" ]; then
    echo "Not found: $dest_name"
    exit 1
  fi
  cp "$src" "prebuilt/$dest_name"
  cp "$src" "$RELDIR/$dest_name"
  echo "Copied -> prebuilt/$dest_name and $RELDIR/$dest_name"
}

copy_one_optional() {
  local src="$1"
  local dest_name="$2"
  if [ -z "$src" ] || [ ! -f "$src" ]; then
    echo "Skip (optional): $dest_name — use sh build.sh build-firmware <env> (companion_radio_usb_tcp* runs mergebin into out/) or pio run -t mergebin -e <env>"
    return 0
  fi
  cp "$src" "prebuilt/$dest_name"
  cp "$src" "$RELDIR/$dest_name"
  echo "Copied -> prebuilt/$dest_name and $RELDIR/$dest_name"
}

# Use newest by mtime so we copy latest build when multiple same-version bins exist
V4_PLAIN=$(ls -t out/heltec_v4_companion_radio_usb_tcp-${VERSION}-*.bin 2>/dev/null | grep -v merged | head -1)
V4_MERGED=$(ls -t out/heltec_v4_companion_radio_usb_tcp-${VERSION}-*-merged.bin 2>/dev/null | head -1)
V4TFT_TOUCH_PLAIN=$(ls -t out/heltec_v4_tft_companion_radio_usb_tcp_touch-${VERSION}-*.bin 2>/dev/null | grep -v merged | head -1)
V4TFT_TOUCH_MERGED=$(ls -t out/heltec_v4_tft_companion_radio_usb_tcp_touch-${VERSION}-*-merged.bin 2>/dev/null | head -1)
V3_PLAIN=$(ls -t out/Heltec_v3_companion_radio_usb_tcp-${VERSION}-*.bin 2>/dev/null | grep -v merged | head -1)
V3_MERGED=$(ls -t out/Heltec_v3_companion_radio_usb_tcp-${VERSION}-*-merged.bin 2>/dev/null | head -1)

copy_one "$V4_PLAIN"   "heltec_v4_companion_radio_usb_tcp.bin"
copy_one_optional "$V4_MERGED"  "heltec_v4_companion_radio_usb_tcp-merged.bin"
copy_one_optional "$V4TFT_TOUCH_PLAIN" "heltec_v4_tft_companion_radio_usb_tcp_touch.bin"
copy_one_optional "$V4TFT_TOUCH_MERGED" "heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin"
copy_one "$V3_PLAIN"   "Heltec_v3_companion_radio_usb_tcp.bin"
copy_one_optional "$V3_MERGED"  "Heltec_v3_companion_radio_usb_tcp-merged.bin"

echo "Done. prebuilt/ and $RELDIR updated."
