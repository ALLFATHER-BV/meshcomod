#!/usr/bin/env bash
# Copy companion USB+TCP build outputs from out/ to prebuilt/ and prebuilt/releases/<version>.
# Run after building:
#   export FIRMWARE_VERSION=v1.14.0.1
#   sh build.sh build-firmware heltec_v4_companion_radio_usb_tcp
#   sh build.sh build-firmware Heltec_v3_companion_radio_usb_tcp
# Usage: sh scripts/copy-release-bins.sh <version>
# Example: sh scripts/copy-release-bins.sh v1.14.0.1

set -e
VERSION="${1:?Usage: $0 <version> e.g. v1.14.0.1}"
RELDIR="prebuilt/releases/${VERSION}"
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

V4_PLAIN=$(ls out/heltec_v4_companion_radio_usb_tcp-${VERSION}-*.bin 2>/dev/null | grep -v merged | head -1)
V4_MERGED=$(ls out/heltec_v4_companion_radio_usb_tcp-${VERSION}-*-merged.bin 2>/dev/null | head -1)
V3_PLAIN=$(ls out/Heltec_v3_companion_radio_usb_tcp-${VERSION}-*.bin 2>/dev/null | grep -v merged | head -1)
V3_MERGED=$(ls out/Heltec_v3_companion_radio_usb_tcp-${VERSION}-*-merged.bin 2>/dev/null | head -1)

copy_one "$V4_PLAIN"   "heltec_v4_companion_radio_usb_tcp.bin"
copy_one "$V4_MERGED"  "heltec_v4_companion_radio_usb_tcp-merged.bin"
copy_one "$V3_PLAIN"   "Heltec_v3_companion_radio_usb_tcp.bin"
copy_one "$V3_MERGED"  "Heltec_v3_companion_radio_usb_tcp-merged.bin"

echo "Done. prebuilt/ and $RELDIR updated."
