# Repeater TCP firmware `.bin` location

After you build **`heltec_v4_repeater_tcp`**, **`Heltec_v3_repeater_tcp`**, **`heltec_v4_tft_repeater_tcp`**, or **`Heltec_Wireless_Paper_repeater_tcp`**, the **app-only** image is copied to **two** places:

| Board | Filename |
|-------|----------|
| **Heltec V4 (OLED)** | `heltec_v4_repeater_tcp.bin` |
| **Heltec V4 TFT** | `heltec_v4_tft_repeater_tcp.bin` |
| **Heltec V3** | `Heltec_v3_repeater_tcp.bin` |
| **Heltec Wireless Paper** | `Heltec_Wireless_Paper_repeater_tcp.bin` |

**Paths (HeltecV4 workspace: repo inside `MeshCore/`):**

1. **`MeshCore/out/`** — next to the PlatformIO project  
2. **`<workspace>/out/`** — one level up (e.g. `HeltecV4/out/`) at the **root of the folder you opened in Cursor**

Each successful link writes **two** files (same bytes):

- **`heltec_v4_repeater_tcp.bin`** — always the latest build (overwritten)  
- **`heltec_v4_repeater_tcp-<YYYYMMDD_HHMMSS>-<gitsha>.bin`** — unique name so you can see **which build** is new (sort by name or mtime)

Same bytes as `.pio/build/<env>/firmware.bin`. Flash the app bin at **0x10000** when bootloader + partitions already match.

**Build:**

```bash
cd MeshCore
python3 -m platformio run -e heltec_v4_repeater_tcp
```

`out/` entries may still be hidden if your editor treats gitignored folders as invisible—use **`HeltecV4/out/`** (workspace root) or **Go to Folder** / Terminal.

## Promoting to `prebuilt/` (same layout as companion)

To match **companion** release folders for your **flasher** or app bundles:

1. Set **`REPEATER_FIRMWARE_VERSION=r1.14.1.0-repeater-tcp`** ( **`r1.14.1.x`** parallels companion **`v1.14.1.x`** ) and run **`sh build.sh build-repeater-firmwares`** so `out/` contains names like **`heltec_v4_repeater_tcp-meshcomod-r1.14.1.0-repeater-tcp-<gitsha>.bin`** (the **`meshcomod-`** prefix is added by **`build.sh`**).
2. Run **`sh scripts/copy-repeater-release-bins.sh r1.14.1.0`** from **`MeshCore/`** so TCP repeater bins live in **`prebuilt/releases/repeater/r1.14.1.0/`** (companions stay in **`prebuilt/releases/companion/v1.14.1.0/`**).

That copies into:

- **`prebuilt/heltec_v4_repeater_tcp.bin`**, **`prebuilt/Heltec_v3_repeater_tcp.bin`**, optional **`prebuilt/heltec_v4_tft_repeater_tcp*.bin`**, optional **`prebuilt/Heltec_Wireless_Paper_repeater_tcp*.bin`** (latest promoted)
- **`prebuilt/releases/repeater/r1.14.1.0/`**

**Legacy layout:** **`repeater-1.0.x`** folders were removed from this repo; **`copy-repeater-release-bins.sh repeater-1.0.x`** still works in forks under **`prebuilt/releases/repeater/`**. **`copy-repeater-release-bins.sh v1.14.1.0`** maps to **`repeater/r1.14.1.0/`** and globs legacy **`v1.14.1.0-repeater-tcp`** in **`out/`**.

Full procedure: [`docs/REPEATER_RELEASE_PROCEDURE.md`](docs/REPEATER_RELEASE_PROCEDURE.md).

Details: [`prebuilt/README.md`](prebuilt/README.md).

See also [`docs/REPEATER_TCP_COMPANION.md`](docs/REPEATER_TCP_COMPANION.md).
