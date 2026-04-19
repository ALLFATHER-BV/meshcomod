## v1.14.1.15 — 2026-03-31

**Firmware version:** v1.14.1.15 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights**

- **Stock MeshCore chat compatibility:** after live V3 chat push, sync-watermark advance is now **client-aware** instead of blanket. Clients identified as stock/unknown keep sync replay available.
- **meshcomod behavior preserved:** clients that identify via `CMD_APP_START` as `mccli` / `meshcomod` keep live-advance behavior to avoid duplicate sync replay.
- **Legacy safety retained:** clients with `CMD_DEVICE_QUERY` protocol `< 3` are still protected by sync adaptation path.
- Includes prior frame hardening from v1.14.1.14 (bounded fixed-width channel/device-info writes and safe channel index fallback).

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.14.1.15`, on-device suffix git **`72f16397`** (compile-time `git rev-parse --short HEAD` when binaries were built).

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
