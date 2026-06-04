#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/build-touch.sh <target>

Targets:
  release  Build Meshcomod Heltec V4 touch firmware (app + merged)
  merged   Alias for release (kept for compatibility)
  all      Alias for release (kept for compatibility)

Environment variables:
  FIRMWARE_VERSION  Optional explicit build tag for output naming.
                    If unset, an automatic UTC timestamp tag is generated.

Examples:
  scripts/build-touch.sh release
  FIRMWARE_VERSION=v1.0.0 scripts/build-touch.sh all
EOF
}

if [[ "${1:-}" == "" ]]; then
  usage
  exit 1
fi

TARGET="$1"
OUT_DIR="out"
mkdir -p "${OUT_DIR}"
SOURCE_DIR="${MESHCOMOD_SOURCE_DIR:-$HOME/meshcomod-touch-src}"
ENV_NAME="heltec_v4_tft_companion_radio_usb_tcp_touch"

if [[ ! -d "${SOURCE_DIR}" ]]; then
  echo "Source not found at ${SOURCE_DIR}; cloning ALLFATHER-BV/meshcomod..."
  git clone https://github.com/ALLFATHER-BV/meshcomod.git "${SOURCE_DIR}"
fi

AUTO_TAG="v$(date -u +%Y.%m.%d-%H%M%S)"
BUILD_TAG="${FIRMWARE_VERSION:-${AUTO_TAG}}"
BUILD_TAG="${BUILD_TAG// /-}"

if ! git -C "${SOURCE_DIR}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "Error: ${SOURCE_DIR} is not a git repository." >&2
  exit 1
fi
SOURCE_SHA="$(git -C "${SOURCE_DIR}" rev-parse --short HEAD)"

echo "Using build tag: ${BUILD_TAG}"

build_release() {
  (
    cd "${SOURCE_DIR}"
    FIRMWARE_VERSION="${BUILD_TAG}" sh build.sh build-firmware "${ENV_NAME}"
  )

  local app_src="${SOURCE_DIR}/out/${ENV_NAME}-${BUILD_TAG}-${SOURCE_SHA}.bin"
  local merged_src="${SOURCE_DIR}/out/${ENV_NAME}-${BUILD_TAG}-${SOURCE_SHA}-merged.bin"
  local app_dst="${OUT_DIR}/${ENV_NAME}-${BUILD_TAG}-${SOURCE_SHA}.bin"
  local merged_dst="${OUT_DIR}/${ENV_NAME}-${BUILD_TAG}-${SOURCE_SHA}-merged.bin"

  if [[ ! -f "${app_src}" ]]; then
    echo "Missing app artifact from meshcomod build: ${app_src}" >&2
    return 1
  fi
  if [[ ! -f "${merged_src}" ]]; then
    echo "Missing merged artifact from meshcomod build: ${merged_src}" >&2
    return 1
  fi

  cp "${app_src}" "${app_dst}"
  cp "${merged_src}" "${merged_dst}"
  echo "Wrote ${app_dst}"
  echo "Wrote ${merged_dst}"
}

case "${TARGET}" in
  release)
    build_release
    ;;
  merged)
    build_release
    ;;
  all)
    build_release
    ;;
  *)
    echo "Unknown target: ${TARGET}" >&2
    usage
    exit 1
    ;;
esac

echo "Done. Artifacts are in ${OUT_DIR}/"
