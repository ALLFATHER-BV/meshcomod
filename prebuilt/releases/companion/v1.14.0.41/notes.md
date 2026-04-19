## v1.14.0.41 — 2026-03-23

**Firmware version:** v1.14.0.41 (meshcomod companion + Heltec V4 extras).

**Highlights**

- **Wi‑Fi‑only HTTP OTA:** `ota url` is accepted only from an active **Wi‑Fi** control path (TCP / WebSocket on multi‑transport, or **SerialWifi** when STA + client are up). USB / BLE initiation is rejected with a clear `ERR:`.
- **OTA minimal mode:** Before HTTPS download, firmware suspends non‑essential transports (e.g. companion stops the non‑active TCP or WS server, releases BLE) while keeping the active Wi‑Fi console for progress; **restore on failure**, reboot on success.
- **Wi‑Fi OTA on the meshcomod repeater image:** Same gating and minimal mode when using the meshcomod CLI over TCP/WS; USB serial `ota url` is rejected.
- **Heltec V4 extras in this folder:** USB / BLE / Wi‑Fi‑only companions (OLED + TFT), plus the usual meshcomod **USB+TCP** (merged + app) and **V3** rows below. **Plain mesh repeaters** from this build live under **[`repeater/r1.14.0.41`](../../repeater/r1.14.0.41/)**.

**Primary prebuilts (flasher / OTA — same as prior releases)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Additional Heltec V4 meshcomod binaries (app / OTA partition)**

| Build | File |
|-------|------|
| OLED companion USB | [heltec_v4_companion_radio_usb.bin](heltec_v4_companion_radio_usb.bin) |
| OLED companion BLE | [heltec_v4_companion_radio_ble.bin](heltec_v4_companion_radio_ble.bin) |
| OLED companion Wi‑Fi | [heltec_v4_companion_radio_wifi.bin](heltec_v4_companion_radio_wifi.bin) |
| TFT companion USB | [heltec_v4_tft_companion_radio_usb.bin](heltec_v4_tft_companion_radio_usb.bin) |
| TFT companion BLE | [heltec_v4_tft_companion_radio_ble.bin](heltec_v4_tft_companion_radio_ble.bin) |
| TFT companion Wi‑Fi | [heltec_v4_tft_companion_radio_wifi.bin](heltec_v4_tft_companion_radio_wifi.bin) |

**Plain mesh repeaters (OLED + TFT):** [`../../repeater/r1.14.0.41/notes.md`](../../repeater/r1.14.0.41/notes.md).

**Build:** `DISABLE_DEBUG=1`. **Git at build:** `872fd926`.

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh), [`scripts/copy-heltec-v4-meshcomod-extras.sh`](../../../scripts/copy-heltec-v4-meshcomod-extras.sh).

**TCP repeater:** for this release era, builds were under legacy **`repeater-1.0.10/`** (removed from repo — **git history**). **Current** TCP repeater prebuilts ship under **`repeater/r1.14.1.0/`** with the meshcomod client / Flasher.
