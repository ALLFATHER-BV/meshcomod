## v1.14.1.14 — 2026-03-31

**Firmware version:** v1.14.1.14 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights**

- **Stock MeshCore compatibility hardening:** channel frames now avoid unsafe fixed-field string writes; `RESP_CODE_CHANNEL_INFO` uses bounded, zero-padded channel-name serialization.
- **Channel message safety:** if a live channel message cannot map to a valid local channel index, companion now emits a safe in-range index instead of `0xFF` to prevent client parser/state failures.
- **Device-info fixed field safety:** build-date field in `RESP_CODE_DEVICE_INFO` now uses bounded fixed-width copy semantics.
- **Sync compatibility kept:** prior selective history watermark advance for legacy protocol clients remains in place.

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.14.1.14`, on-device suffix git **`a9c2d75d`** (compile-time `git rev-parse --short HEAD` when binaries were built).

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
