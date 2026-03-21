#!/usr/bin/env bash
# Copy TCP repeater build outputs from out/ to prebuilt/ and prebuilt/releases/<version>.
# Same layout as scripts/copy-release-bins.sh (companion).
#
# Run after building with a repeater-specific version (independent of companion v1.14.x):
#   export REPEATER_FIRMWARE_VERSION=repeater-1.0.0
#   sh build.sh build-repeater-firmwares
#   # or single env (REPEATER_FIRMWARE_VERSION still applies):
#   sh build.sh build-firmware heltec_v4_repeater_tcp
#
# Usage: sh scripts/copy-repeater-release-bins.sh <version>
# Example: sh scripts/copy-repeater-release-bins.sh repeater-1.0.0

set -e
VERSION="${1:?Usage: $0 <repeater-version> e.g. repeater-1.0.0}"
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

copy_one_optional() {
  local src="$1"
  local dest_name="$2"
  if [ -z "$src" ] || [ ! -f "$src" ]; then
    echo "Skip (optional): $dest_name — run pio run -t mergebin and copy merged to out/ with name matching *-${VERSION}-*-merged.bin if you need it"
    return 0
  fi
  cp "$src" "prebuilt/$dest_name"
  cp "$src" "$RELDIR/$dest_name"
  echo "Copied -> prebuilt/$dest_name and $RELDIR/$dest_name"
}

# Newest by mtime when multiple same-version bins exist (matches copy-release-bins.sh)
V4_PLAIN=$(ls -t out/heltec_v4_repeater_tcp-${VERSION}-*.bin 2>/dev/null | grep -v merged | head -1)
V4_MERGED=$(ls -t out/heltec_v4_repeater_tcp-${VERSION}-*-merged.bin 2>/dev/null | head -1)
V3_PLAIN=$(ls -t out/Heltec_v3_repeater_tcp-${VERSION}-*.bin 2>/dev/null | grep -v merged | head -1)
V3_MERGED=$(ls -t out/Heltec_v3_repeater_tcp-${VERSION}-*-merged.bin 2>/dev/null | head -1)

copy_one "$V4_PLAIN" "heltec_v4_repeater_tcp.bin"
copy_one_optional "$V4_MERGED" "heltec_v4_repeater_tcp-merged.bin"
copy_one "$V3_PLAIN" "Heltec_v3_repeater_tcp.bin"
copy_one_optional "$V3_MERGED" "Heltec_v3_repeater_tcp-merged.bin"

echo "Done. prebuilt/ and $RELDIR updated."
