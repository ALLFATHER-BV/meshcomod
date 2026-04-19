#!/usr/bin/env bash
# Copy TCP repeater build outputs from out/ to prebuilt/ and prebuilt/releases/repeater/<dir>.
#
# **`build-repeater-firmwares`** builds every env ending in **`_repeater_tcp`** (Heltec V4 OLED, V4 TFT, V3, Wireless Paper).
#
# **Recommended (meshcomod `main`):** repeater release dirs use **`r*`** parallel to companion **`v*`**:
#   export REPEATER_FIRMWARE_VERSION=r1.15.0.2-repeater-tcp
#   sh build.sh build-repeater-firmwares
#   sh scripts/copy-repeater-release-bins.sh r1.15.0.2
# → matches `out/*-meshcomod-r1.15.0.2-repeater-tcp-<sha>.bin` → **prebuilt/releases/repeater/r1.15.0.2/**.
#
# You can pass **r1.14.1.0-repeater-tcp** explicitly (same result).
#
# **Backward compatibility:** **`v1.14.1.0`** or **`v1.14.1.0-repeater-tcp`** still work — release dir is **`r1.14.1.0`**
# (glob matches legacy **`v1.14.1.0-repeater-tcp`** in `out/` filenames).
#
# **Legacy:** **`repeater-1.0.x`** → **prebuilt/releases/repeater/repeater-1.0.x/**.
#
# Usage: sh scripts/copy-repeater-release-bins.sh <r-base|v-base|full-repeater-tag|legacy>
# Examples:
#   sh scripts/copy-repeater-release-bins.sh r1.14.1.0
#   sh scripts/copy-repeater-release-bins.sh r1.14.1.0-repeater-tcp
#   sh scripts/copy-repeater-release-bins.sh v1.14.1.0
#   sh scripts/copy-repeater-release-bins.sh repeater-1.0.0

set -e
VERSION_ARG="${1:?Usage: $0 <version> e.g. r1.14.1.0 or v1.14.1.0 (maps to r1.14.1.0) or repeater-1.0.0}"

if [[ "$VERSION_ARG" == repeater-* ]]; then
  RELDIR_VERSION="$VERSION_ARG"
  GLOB_VERSION="$VERSION_ARG"
elif [[ "$VERSION_ARG" =~ ^r[0-9]+[.][0-9]+[.][0-9]+[.][0-9]+$ ]]; then
  RELDIR_VERSION="$VERSION_ARG"
  GLOB_VERSION="${VERSION_ARG}-repeater-tcp"
elif [[ "$VERSION_ARG" == r*-repeater-tcp ]]; then
  RELDIR_VERSION="${VERSION_ARG%-repeater-tcp}"
  GLOB_VERSION="$VERSION_ARG"
elif [[ "$VERSION_ARG" =~ ^v[0-9]+[.][0-9]+[.][0-9]+[.][0-9]+$ ]]; then
  RELDIR_VERSION="r${VERSION_ARG#v}"
  GLOB_VERSION="${VERSION_ARG}-repeater-tcp"
elif [[ "$VERSION_ARG" == v*-repeater-tcp ]]; then
  base="${VERSION_ARG%-repeater-tcp}"
  RELDIR_VERSION="r${base#v}"
  GLOB_VERSION="$VERSION_ARG"
else
  RELDIR_VERSION="$VERSION_ARG"
  GLOB_VERSION="$VERSION_ARG"
fi

RELDIR="prebuilt/releases/repeater/${RELDIR_VERSION}"
mkdir -p "prebuilt" "$RELDIR"
echo "Repeater copy: matching out/*-${GLOB_VERSION}-*.bin → ${RELDIR}/"

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
    echo "Skip (optional): $dest_name — sh build.sh build-firmware <env> runs mergebin for *_repeater_tcp into out/, or pio run -t mergebin -e <env>"
    return 0
  fi
  cp "$src" "prebuilt/$dest_name"
  cp "$src" "$RELDIR/$dest_name"
  echo "Copied -> prebuilt/$dest_name and $RELDIR/$dest_name"
}

V4_PLAIN=$(ls -t out/heltec_v4_repeater_tcp-"${GLOB_VERSION}"-*.bin out/heltec_v4_repeater_tcp-meshcomod-"${GLOB_VERSION}"-*.bin 2>/dev/null | grep -v merged | head -1)
V4_MERGED=$(ls -t out/heltec_v4_repeater_tcp-"${GLOB_VERSION}"-*-merged.bin out/heltec_v4_repeater_tcp-meshcomod-"${GLOB_VERSION}"-*-merged.bin 2>/dev/null | head -1)
V4TFT_PLAIN=$(ls -t out/heltec_v4_tft_repeater_tcp-"${GLOB_VERSION}"-*.bin out/heltec_v4_tft_repeater_tcp-meshcomod-"${GLOB_VERSION}"-*.bin 2>/dev/null | grep -v merged | head -1)
V4TFT_MERGED=$(ls -t out/heltec_v4_tft_repeater_tcp-"${GLOB_VERSION}"-*-merged.bin out/heltec_v4_tft_repeater_tcp-meshcomod-"${GLOB_VERSION}"-*-merged.bin 2>/dev/null | head -1)
V3_PLAIN=$(ls -t out/Heltec_v3_repeater_tcp-"${GLOB_VERSION}"-*.bin out/Heltec_v3_repeater_tcp-meshcomod-"${GLOB_VERSION}"-*.bin 2>/dev/null | grep -v merged | head -1)
V3_MERGED=$(ls -t out/Heltec_v3_repeater_tcp-"${GLOB_VERSION}"-*-merged.bin out/Heltec_v3_repeater_tcp-meshcomod-"${GLOB_VERSION}"-*-merged.bin 2>/dev/null | head -1)
PAPER_PLAIN=$(ls -t out/Heltec_Wireless_Paper_repeater_tcp-"${GLOB_VERSION}"-*.bin out/Heltec_Wireless_Paper_repeater_tcp-meshcomod-"${GLOB_VERSION}"-*.bin 2>/dev/null | grep -v merged | head -1)
PAPER_MERGED=$(ls -t out/Heltec_Wireless_Paper_repeater_tcp-"${GLOB_VERSION}"-*-merged.bin out/Heltec_Wireless_Paper_repeater_tcp-meshcomod-"${GLOB_VERSION}"-*-merged.bin 2>/dev/null | head -1)

copy_one "$V4_PLAIN" "heltec_v4_repeater_tcp.bin"
copy_one_optional "$V4_MERGED" "heltec_v4_repeater_tcp-merged.bin"
copy_one_optional "$V4TFT_PLAIN" "heltec_v4_tft_repeater_tcp.bin"
copy_one_optional "$V4TFT_MERGED" "heltec_v4_tft_repeater_tcp-merged.bin"
copy_one "$V3_PLAIN" "Heltec_v3_repeater_tcp.bin"
copy_one_optional "$V3_MERGED" "Heltec_v3_repeater_tcp-merged.bin"
copy_one_optional "$PAPER_PLAIN" "Heltec_Wireless_Paper_repeater_tcp.bin"
copy_one_optional "$PAPER_MERGED" "Heltec_Wireless_Paper_repeater_tcp-merged.bin"

echo "Done. prebuilt/ and $RELDIR updated."
