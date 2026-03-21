# Prebuilt firmware (flasher / releases)

Binaries are **not** always committed; this folder defines **where promoted builds live** so tools and humans match the same layout.

## Layout (same for companion and repeater TCP)

| Path | Meaning |
|------|--------|
| **`prebuilt/<short-name>.bin`** | Latest **promoted** build for that product (overwritten when you run the copy script for a new release). |
| **`prebuilt/releases/<version>/`** | **Immutable** copy for that version (e.g. `v1.14.0.20`). Add a `notes.md` here when you cut a release. |

**Companion** (USB+TCP meshcomod radio): after versioned builds in `out/`, run:

```bash
sh scripts/copy-release-bins.sh <version>
```

Produces e.g. `prebuilt/heltec_v4_companion_radio_usb_tcp.bin` and `prebuilt/releases/<version>/…`.

**Repeater TCP** (Heltec V4 / V3 Wi‑Fi companion subset): after versioned builds in `out/`, run:

```bash
sh scripts/copy-repeater-release-bins.sh <version>
```

Produces:

- `prebuilt/heltec_v4_repeater_tcp.bin`
- `prebuilt/Heltec_v3_repeater_tcp.bin`
- Same filenames under `prebuilt/releases/<version>/`

**Optional merged** images (`*-merged.bin`, flash from **0x0**): only appear if you run `pio run -t mergebin` and place a matching `*-merged.bin` in `out/` before the copy script (see script comments).

## Build inputs (versioned `out/` names)

`build.sh build-firmware …` requires **`FIRMWARE_VERSION`** in the environment and writes:

`out/<env>-<version>-<gitsha>.bin`

Example:

```bash
export FIRMWARE_VERSION=v1.14.0.20
sh build.sh build-repeater-firmwares
sh scripts/copy-repeater-release-bins.sh v1.14.0.20
```

Ad-hoc **`pio run`** (without `build.sh`) still drops **`out/<env>.bin`** and stamped copies via `merge-bin.py`; those names **do not** match the copy scripts — use **`build.sh`** (or manual rename) before promoting to `prebuilt/`.

See also: [`docs/RELEASE_PROCEDURE.md`](../docs/RELEASE_PROCEDURE.md) (companion), [`WHERE_IS_REPEATER_FIRMWARE.md`](../WHERE_IS_REPEATER_FIRMWARE.md), [`docs/REPEATER_TCP_COMPANION.md`](../docs/REPEATER_TCP_COMPANION.md).
