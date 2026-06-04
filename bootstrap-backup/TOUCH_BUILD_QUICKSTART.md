# Touch Build Quickstart

## Prerequisites

- `python3`
- PlatformIO (`pio` in PATH or `python3 -m platformio`)

Install example:

```bash
python3 -m pip install --user -U platformio
```

## Build targets

Default command (builds both app-only and merged release binaries):

```bash
./build
```

Optional version tagging:

```bash
export FIRMWARE_VERSION="v1.0.0"
./build
```

If `FIRMWARE_VERSION` is not set, the build scripts auto-generate a tag each run
(UTC timestamp), so outputs are always tagged.

Build source of truth:

- `./build` delegates to `${HOME}/meshcomod-touch-src` (or `MESHCOMOD_SOURCE_DIR`) and builds env `heltec_v4_tft_companion_radio_usb_tcp_touch`.
- This keeps output equivalent to regular Meshcomod V4 touch builds.

Advanced helper script targets:

```bash
scripts/build-touch.sh release
scripts/build-touch.sh merged
scripts/build-touch.sh all
```

Artifacts are written to:

- `out/heltec_v4_touch_dev-<version>-<sha>.bin`
- `out/heltec_v4_touch_release-<version>-<sha>.bin`
- `out/heltec_v4_touch_release-<version>-<sha>-merged.bin`

## Flashing notes

- Prefer merged binary for first-time flash and recovery cases.
- Non-merged app binaries require a valid bootloader/partition setup already present.
- Keep one known-good prior merged build for rollback safety.
- WiFi provisioning is runtime-only (post-flash), same as regular Meshcomod.
