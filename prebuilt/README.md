# Prebuilt firmware (flasher / releases)

Binaries are **not** always committed; this folder defines **where promoted builds live** so tools and humans match the same layout.

## Layout

| Path | Meaning |
|------|--------|
| **`prebuilt/<short-name>.bin`** | Latest **promoted** build **for that product** (companion copy vs repeater copy touch **different** stable names). |
| **`prebuilt/releases/companion/v*…/`** | **Immutable** companion drop (USB+TCP row, TFT touch, V3, companion extras). |
| **`prebuilt/releases/repeater/r*…/`** | **Immutable** repeater drop (plain mesh + TCP repeater). Release id **`r*`** parallels companion **`v*`** (same numbers, **`r`** prefix). Read **[`releases/README.md`](releases/README.md)**. |

**Partial folders:** A companion folder may ship **without** a matching repeater folder (and vice versa). Copy scripts **add/overwrite their own files** and do not delete other artifacts.

**Companion** versions: **`vX.Y.Z.W`** under **`companion/`**. **Repeater TCP:** **`rX.Y.Z.W`** under **`repeater/`**, built with **`REPEATER_FIRMWARE_VERSION=rX.Y.Z.W-repeater-tcp`** (legacy **`vX.Y.Z.W-repeater-tcp`** in `out/` still works with **`copy-repeater-release-bins.sh vX.Y.Z.W`**). The meshcomod client lists **`prebuilt/releases/repeater`** for the repeater product (see **`meshcomod-client`** `shared/firmware_catalog.ts`). Legacy **`repeater-1.0.x`** may live under **`repeater/`** in forks.

Agent checklist: **[`../AGENTS.md`](../AGENTS.md)**.

**Companion** (USB+TCP meshcomod radio): after versioned builds in `out/`, run:

```bash
sh scripts/copy-release-bins.sh <version>
```

Produces e.g. `prebuilt/heltec_v4_companion_radio_usb_tcp.bin` and `prebuilt/releases/companion/<version>/…`.

**Heltec V4 meshcomod extras** (OLED/TFT **USB-only**, **BLE**, **Wi‑Fi** companions; **plain** OLED/TFT repeaters — not the meshcomod USB+TCP row above): after the same `FIRMWARE_VERSION` builds in `out/`, run:

```bash
sh scripts/copy-heltec-v4-meshcomod-extras.sh <version>
```

Copies stable names into `prebuilt/`, `prebuilt/releases/companion/<version>/`, and plain repeaters into **`prebuilt/releases/repeater/r<same>/`**. See [`releases/companion/v1.14.1.0/notes.md`](releases/companion/v1.14.1.0/notes.md) and [`releases/repeater/r1.14.1.0/notes.md`](releases/repeater/r1.14.1.0/notes.md).

**Repeater TCP** (Heltec V4 / V3 Wi‑Fi companion subset): build with **`REPEATER_FIRMWARE_VERSION=r1.14.1.1-repeater-tcp`** (or legacy **`v1.14.1.x-repeater-tcp`** / **`r1.14.1.0-repeater-tcp`**), then:

```bash
sh scripts/copy-repeater-release-bins.sh r1.14.1.1
```

Produces / updates:

- `prebuilt/heltec_v4_repeater_tcp.bin`, optional **`heltec_v4_tft_repeater_tcp.bin`** (+ merged variants when built)
- `prebuilt/Heltec_v3_repeater_tcp.bin`
- Same stable names under **`prebuilt/releases/repeater/r1.14.1.1/`** (older pins remain under **`r1.14.1.0/`**, etc.)

**Merged** images (`*-merged.bin`, flash from **0x0**): `build.sh build-firmware` / `build-repeater-firmwares` runs `mergebin` for `companion_radio_usb_tcp*` and `*_repeater_tcp` envs and copies `*-merged.bin` into `out/` before you run the copy script.

## Build inputs (versioned `out/` names)

`build.sh build-firmware …` requires **`FIRMWARE_VERSION`** in the environment and writes:

`out/<env>-<version>-<gitsha>.bin`

Repeater example (folder **`r1.14.1.1`** — may match companion **`v1.14.1.1`** or be a repeater-only pin):

```bash
export REPEATER_FIRMWARE_VERSION=r1.14.1.1-repeater-tcp
sh build.sh build-repeater-firmwares
sh scripts/copy-repeater-release-bins.sh r1.14.1.1
```

Ad-hoc **`pio run`** (without `build.sh`) still drops **`out/<env>.bin`** and stamped copies via `merge-bin.py`; those names **do not** match the copy scripts — use **`build.sh`** (or manual rename) before promoting to `prebuilt/`.

See also: [`docs/RELEASE_PROCEDURE.md`](../docs/RELEASE_PROCEDURE.md) (companion), [`docs/REPEATER_RELEASE_PROCEDURE.md`](../docs/REPEATER_RELEASE_PROCEDURE.md) (repeater), [`WHERE_IS_REPEATER_FIRMWARE.md`](../WHERE_IS_REPEATER_FIRMWARE.md), [`docs/REPEATER_TCP_COMPANION.md`](../docs/REPEATER_TCP_COMPANION.md).
