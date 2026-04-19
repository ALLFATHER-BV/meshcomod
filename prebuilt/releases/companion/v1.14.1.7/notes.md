## v1.14.1.7 — 2026-03-29

**Firmware version:** v1.14.1.7 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights**

- **URL OTA + meshcomod client:** restores **preserve TCP or WebSocket** during HTTPS OTA (whichever session started `ota url`) so the **client stays connected** for progress and the final OK/reboot message. **v1.14.1.6** suspended both transports and caused an immediate disconnect.
- **HTTPS robustness (kept from v1.14.1.6):** **fresh HTTP/TLS session** is still built before the **flasher.meshcomod.com** fallback after a failed raw-GitHub fetch.
- **Companion:** OTA status lines remain **mirrored to USB Serial** for debugging.
- **Repeater TCP companion:** **path-based** suspend again (TCP path drops WS only; WS path drops TCP only).

**Skip note:** Skip if you never installed **v1.14.1.6** and URL OTA + client UX already works for you on **v1.14.1.5** or earlier. **Upgrade from v1.14.1.6** if Start OTA disconnects the client.

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.14.1.7`.

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
