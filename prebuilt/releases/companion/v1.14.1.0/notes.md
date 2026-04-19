## v1.14.1.0 — 2026-03-23

**Firmware version:** v1.14.1.0 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights**

- **Upstream sync:** Merged official MeshCore **v1.14.1** (`467959cc`) — duty-cycle / dispatcher updates, SX126x RX boosted gain API, room/sensor/repeater fixes, new boards, docs.
- **Meshcomod preserved:** Multi-transport companion (USB / BLE / TCP / WebSocket), per-client sync, WiFi runtime + console, **Wi‑Fi‑only HTTP OTA** with minimal transport mode and restore-on-failure, companion CLI on the meshcomod **Wi‑Fi repeater** image, Heltec V4/V3 variants and prebuilt layout unchanged in behavior.
- **This folder (companion only):** USB / BLE / Wi‑Fi‑only companions (OLED + TFT), plus meshcomod **USB+TCP** (merged + app) and **V3** rows below. **All `*repeater*` binaries** are pinned under **[`repeater/r1.14.1.0`](../../repeater/r1.14.1.0/)** (release id **`r1.14.1.0`**).

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

**Repeaters:** plain mesh + TCP repeater — see **[`../../repeater/r1.14.1.0/notes.md`](../../repeater/r1.14.1.0/notes.md)**.

**Build:** `DISABLE_DEBUG=1`. **Companion + Heltec V4 companion extras** images were compiled at git **`47c3fb1c`** (see `out/…-v1.14.1.0-47c3fb1c…`).

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh), [`scripts/copy-heltec-v4-meshcomod-extras.sh`](../../../scripts/copy-heltec-v4-meshcomod-extras.sh), [`scripts/copy-repeater-release-bins.sh`](../../../scripts/copy-repeater-release-bins.sh).
