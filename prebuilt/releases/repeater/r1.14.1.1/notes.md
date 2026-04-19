## r1.14.1.1 — TCP repeater (meshcomod companion **v1.14.1.1** train)

**Release id:** **`r1.14.1.1`** — Heltec **Wi‑Fi / TCP+WebSocket** repeater images (`*_repeater_tcp`). Plain mesh repeaters (non‑TCP) for the **1.14.1.x** train may still be listed under [`../r1.14.1.0/`](../r1.14.1.0/) until refreshed via [`scripts/copy-heltec-v4-meshcomod-extras.sh`](../../../../scripts/copy-heltec-v4-meshcomod-extras.sh).

**On-device version string:** **`meshcomod-r1.14.1.1-repeater-tcp-<gitsha>`** (build with **`REPEATER_FIRMWARE_VERSION=r1.14.1.1-repeater-tcp`**).

**Highlights (this drop)**

- **SNTP (UTC):** after Wi‑Fi gets an IP, firmware starts time sync (`pool.ntp.org`, `time.cloudflare.com`); clock stays UTC for mesh timestamps.
- **meshcomod repeater web app:** browser clock sync, locale time display, decimal lat/lon + map / geolocation (see meshcomod-client `repeater_main.ts`). Use **HTTP** [`repeater.meshcomod.com`](https://repeater.meshcomod.com) for `ws://` to the device.

**Primary TCP repeater binaries**

| Device | Merged (0x0, USB flasher) | App-only (Wi‑Fi OTA) |
|--------|---------------------------|----------------------|
| Heltec V4 (OLED) | [heltec_v4_repeater_tcp-merged.bin](heltec_v4_repeater_tcp-merged.bin) | [heltec_v4_repeater_tcp.bin](heltec_v4_repeater_tcp.bin) |
| Heltec V4 TFT | [heltec_v4_tft_repeater_tcp-merged.bin](heltec_v4_tft_repeater_tcp-merged.bin) | [heltec_v4_tft_repeater_tcp.bin](heltec_v4_tft_repeater_tcp.bin) |
| Heltec V3 | [Heltec_v3_repeater_tcp-merged.bin](Heltec_v3_repeater_tcp-merged.bin) | [Heltec_v3_repeater_tcp.bin](Heltec_v3_repeater_tcp.bin) |

**Build / promote**

```bash
cd MeshCore
export REPEATER_FIRMWARE_VERSION=r1.14.1.1-repeater-tcp
export DISABLE_DEBUG=1
sh build.sh build-repeater-firmwares
sh scripts/copy-repeater-release-bins.sh r1.14.1.1
```

**This promotion:** `DISABLE_DEBUG=1`, **`REPEATER_FIRMWARE_VERSION=r1.14.1.1-repeater-tcp`**, git **`e5973fcc`** (`out/…-meshcomod-r1.14.1.1-repeater-tcp-e5973fcc…`).

**Procedure:** [`docs/REPEATER_RELEASE_PROCEDURE.md`](../../../../docs/REPEATER_RELEASE_PROCEDURE.md), [`prebuilt/README.md`](../../../README.md).
