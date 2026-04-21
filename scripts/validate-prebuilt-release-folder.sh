#!/usr/bin/env bash
# Sanity-check prebuilt/releases/<companion|repeater|rooms>/<version>/ against notes.md claims.
# Run from MeshCore repo root after copying binaries.
#
# Usage: sh scripts/validate-prebuilt-release-folder.sh companion v1.14.1.1
#        sh scripts/validate-prebuilt-release-folder.sh repeater r1.14.1.0
#        sh scripts/validate-prebuilt-release-folder.sh rooms r1.15.0.0

set -e
KIND="${1:?Usage: $0 <companion|repeater|rooms> <version> e.g. companion v1.14.1.1 or repeater r1.14.1.0 or rooms r1.15.0.0}"
VERSION="${2:?Usage: $0 $KIND <version>}"

case "$KIND" in
  companion|repeater|rooms) ;;
  *)
    echo "First arg must be companion, repeater, or rooms, got: $KIND" >&2
    exit 1
    ;;
esac

REL="prebuilt/releases/${KIND}/${VERSION}"

if [ ! -d "$REL" ]; then
  echo "Directory not found: $REL" >&2
  exit 1
fi

NOTES="${REL}/notes.md"
if [ ! -f "$NOTES" ]; then
  echo "OK (no notes.md to check): $REL"
  exit 0
fi

# Companion notes sometimes mention repeater TCP; require the bin only in companion folders (not rooms/repeater).
if [ "$KIND" = "companion" ] && grep -qiE 'repeater.tcp|repeater_tcp|heltec_v4_repeater_tcp' "$NOTES"; then
  if [ ! -f "${REL}/heltec_v4_repeater_tcp.bin" ]; then
    echo "ERROR: $NOTES mentions TCP repeater but ${REL}/heltec_v4_repeater_tcp.bin is missing." >&2
    exit 1
  fi
fi

if [ "$KIND" = "companion" ] && grep -qiF "heltec_v4_companion_radio_usb_tcp.bin" "$NOTES"; then
  if [ ! -f "${REL}/heltec_v4_companion_radio_usb_tcp.bin" ]; then
    echo "ERROR: $NOTES references heltec_v4_companion_radio_usb_tcp.bin but file is missing in $REL." >&2
    exit 1
  fi
fi

if [ "$KIND" = "companion" ] && grep -qiF "Heltec_Wireless_Paper_companion_radio_usb_tcp.bin" "$NOTES"; then
  if [ ! -f "${REL}/Heltec_Wireless_Paper_companion_radio_usb_tcp.bin" ]; then
    echo "ERROR: $NOTES references Heltec_Wireless_Paper_companion_radio_usb_tcp.bin but file is missing in $REL." >&2
    exit 1
  fi
fi

if [ "$KIND" = "repeater" ]; then
  if grep -qiE 'Heltec_Wireless_Paper_repeater_tcp|Paper.*repeater.tcp|Wireless Paper.*repeater' "$NOTES"; then
    if [ ! -f "${REL}/Heltec_Wireless_Paper_repeater_tcp.bin" ]; then
      echo "ERROR: $NOTES mentions Wireless Paper repeater TCP but ${REL}/Heltec_Wireless_Paper_repeater_tcp.bin is missing." >&2
      exit 1
    fi
  fi
fi

if [ "$KIND" = "rooms" ]; then
  if grep -qiE 'heltec_v4_room_server_multitransport|room.*multitransport' "$NOTES"; then
    if [ ! -f "${REL}/heltec_v4_room_server_multitransport.bin" ]; then
      echo "ERROR: $NOTES mentions V4 OLED room multitransport but ${REL}/heltec_v4_room_server_multitransport.bin is missing." >&2
      exit 1
    fi
  fi
  if grep -qiE 'Heltec_Wireless_Paper_room_server_multitransport|Paper.*room.*multitransport' "$NOTES"; then
    if [ ! -f "${REL}/Heltec_Wireless_Paper_room_server_multitransport.bin" ]; then
      echo "ERROR: $NOTES mentions Wireless Paper room multitransport but ${REL}/Heltec_Wireless_Paper_room_server_multitransport.bin is missing." >&2
      exit 1
    fi
  fi
  if grep -qiE 'Xiao_S3_WIO_room_server_multitransport|Xiao.*room.*multitransport' "$NOTES"; then
    if [ ! -f "${REL}/Xiao_S3_WIO_room_server_multitransport.bin" ]; then
      echo "ERROR: $NOTES mentions Xiao S3 WIO room multitransport but ${REL}/Xiao_S3_WIO_room_server_multitransport.bin is missing." >&2
      exit 1
    fi
  fi
fi

echo "OK: $REL matches notes.md claims (spot checks)."
