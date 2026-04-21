#!/usr/bin/env bash
# Copy meshcomod room multitransport build outputs from out/ to prebuilt/ and prebuilt/releases/rooms/<dir>.
#
# **`build-room-multitransport-firmwares`** builds every env ending in **`_room_server_multitransport`**.
#
# **Recommended:** set **`ROOM_FIRMWARE_VERSION=rX.Y.Z.W-room-mt`** (parallel naming to repeater **`-repeater-tcp`**), then:
#   export ROOM_FIRMWARE_VERSION=r1.15.0.0-room-mt
#   sh build.sh build-room-multitransport-firmwares
#   sh scripts/copy-room-release-bins.sh r1.15.0.0
# → matches `out/*-meshcomod-r1.15.0.0-room-mt-<sha>.bin` → **prebuilt/releases/rooms/r1.15.0.0/**.
#
# You can pass **r1.15.0.0-room-mt** explicitly (same glob segment as in out/).
#
# Usage: sh scripts/copy-room-release-bins.sh <r-base|full-room-mt-tag>
# Examples:
#   sh scripts/copy-room-release-bins.sh r1.15.0.0
#   sh scripts/copy-room-release-bins.sh r1.15.0.0-room-mt

set -e
VERSION_ARG="${1:?Usage: $0 <version> e.g. r1.15.0.0 (maps glob to r1.15.0.0-room-mt)}"

if [[ "$VERSION_ARG" =~ ^r[0-9]+[.][0-9]+[.][0-9]+[.][0-9]+$ ]]; then
  RELDIR_VERSION="$VERSION_ARG"
  GLOB_VERSION="${VERSION_ARG}-room-mt"
elif [[ "$VERSION_ARG" == r*-room-mt ]]; then
  RELDIR_VERSION="${VERSION_ARG%-room-mt}"
  GLOB_VERSION="$VERSION_ARG"
else
  RELDIR_VERSION="$VERSION_ARG"
  GLOB_VERSION="$VERSION_ARG"
fi

RELDIR="prebuilt/releases/rooms/${RELDIR_VERSION}"
mkdir -p "prebuilt" "$RELDIR"
echo "Room multitransport copy: matching out/*-${GLOB_VERSION}-*.bin → ${RELDIR}/"

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
    echo "Skip (optional): $dest_name — run build.sh build-room-multitransport-firmwares (mergebin for *_room_server_multitransport)"
    return 0
  fi
  cp "$src" "prebuilt/$dest_name"
  cp "$src" "$RELDIR/$dest_name"
  echo "Copied -> prebuilt/$dest_name and $RELDIR/$dest_name"
}

V4_PLAIN=$(ls -t out/heltec_v4_room_server_multitransport-meshcomod-"${GLOB_VERSION}"-*.bin 2>/dev/null | grep -v merged | head -1)
V4_MERGED=$(ls -t out/heltec_v4_room_server_multitransport-meshcomod-"${GLOB_VERSION}"-*-merged.bin 2>/dev/null | head -1)
V4TFT_PLAIN=$(ls -t out/heltec_v4_tft_room_server_multitransport-meshcomod-"${GLOB_VERSION}"-*.bin 2>/dev/null | grep -v merged | head -1)
V4TFT_MERGED=$(ls -t out/heltec_v4_tft_room_server_multitransport-meshcomod-"${GLOB_VERSION}"-*-merged.bin 2>/dev/null | head -1)
V3_PLAIN=$(ls -t out/Heltec_v3_room_server_multitransport-meshcomod-"${GLOB_VERSION}"-*.bin 2>/dev/null | grep -v merged | head -1)
V3_MERGED=$(ls -t out/Heltec_v3_room_server_multitransport-meshcomod-"${GLOB_VERSION}"-*-merged.bin 2>/dev/null | head -1)
PAPER_PLAIN=$(ls -t out/Heltec_Wireless_Paper_room_server_multitransport-meshcomod-"${GLOB_VERSION}"-*.bin 2>/dev/null | grep -v merged | head -1)
PAPER_MERGED=$(ls -t out/Heltec_Wireless_Paper_room_server_multitransport-meshcomod-"${GLOB_VERSION}"-*-merged.bin 2>/dev/null | head -1)
XIAO_PLAIN=$(ls -t out/Xiao_S3_WIO_room_server_multitransport-meshcomod-"${GLOB_VERSION}"-*.bin 2>/dev/null | grep -v merged | head -1)
XIAO_MERGED=$(ls -t out/Xiao_S3_WIO_room_server_multitransport-meshcomod-"${GLOB_VERSION}"-*-merged.bin 2>/dev/null | head -1)

copy_one "$V4_PLAIN" "heltec_v4_room_server_multitransport.bin"
copy_one_optional "$V4_MERGED" "heltec_v4_room_server_multitransport-merged.bin"
copy_one_optional "$V4TFT_PLAIN" "heltec_v4_tft_room_server_multitransport.bin"
copy_one_optional "$V4TFT_MERGED" "heltec_v4_tft_room_server_multitransport-merged.bin"
copy_one "$V3_PLAIN" "Heltec_v3_room_server_multitransport.bin"
copy_one_optional "$V3_MERGED" "Heltec_v3_room_server_multitransport-merged.bin"
copy_one_optional "$PAPER_PLAIN" "Heltec_Wireless_Paper_room_server_multitransport.bin"
copy_one_optional "$PAPER_MERGED" "Heltec_Wireless_Paper_room_server_multitransport-merged.bin"
copy_one_optional "$XIAO_PLAIN" "Xiao_S3_WIO_room_server_multitransport.bin"
copy_one_optional "$XIAO_MERGED" "Xiao_S3_WIO_room_server_multitransport-merged.bin"

echo "Done. prebuilt/ and $RELDIR updated."
