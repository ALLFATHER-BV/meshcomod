## v1.14.1.8 — 2026-03-29

**Firmware version:** v1.14.1.8 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights**

- **HTTPS URL OTA from WebSocket:** while keeping the **existing WS client** connected (same as v1.14.1.7), OTA now **pauses only the WebSocket listen socket** (`pauseListen` / `resumeListen`). That avoids lwIP contention between **`WiFiServer` (accept)** and **`WiFiClientSecure`** that could **freeze the device** on the **flasher.meshcomod.com** fallback after raw GitHub.
- **Repeater TCP companion:** same **pause WS listen** when URL OTA is started on the WebSocket path.
- **Unchanged:** TCP control path, BLE teardown before OTA, **fresh TLS/HTTP session** before flasher fallback, USB Serial OTA line mirror.

**Skip note:** If you use **`ota url` from WebSocket** and saw a hang after **`OTA: diag try flasher.meshcomod.com`** on **v1.14.1.7** or earlier, upgrade. If you only OTA from **TCP** or USB and are stable on **v1.14.1.7**, you can skip.

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.14.1.8`.

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
