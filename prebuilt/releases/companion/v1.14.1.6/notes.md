## v1.14.1.6 — 2026-03-29

**Firmware version:** v1.14.1.6 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights**

- **HTTPS URL OTA reliability:** companion **suspends both TCP and WebSocket** (and BLE as before) during the HTTPS fetch so TLS does not contend with the Wi‑Fi listener stack. Fixes stalls after `OTA: diag try flasher.meshcomod.com` when OTA was started from WebSocket (also seen on **v1.14.1.4** / **v1.14.1.5**).
- **Flasher fallback:** after a failed raw-GitHub attempt, OTA **rebuilds the HTTP/TLS client** before hitting `flasher.meshcomod.com`.
- **USB visibility:** OTA progress lines are **mirrored to USB Serial** during multi-transport companion builds so you can follow the log while Wi‑Fi transports are down.
- **Repeater TCP companion:** same suspend-both-transports behavior for URL OTA.

**Skip note:** If you never use **HTTPS URL OTA** from Wi‑Fi and are stable on **v1.14.1.5**, you can skip; anyone using **`ota url https://…`** over TCP or WebSocket should upgrade.

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.14.1.6`.

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
