## v1.14.0.17 — 2026-02-24

**Firmware version:** v1.14.0.17 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS handshake buffer 1536 bytes:** Fixes "unexpectedly closed the connection" when opening https://device:8765 or connecting via wss:// (browser/client requests no longer truncated).
- **WSS/WS tab always visible:** Tab after WiFi shows for all companion builds; label "WSS" or "WS" by build.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.17/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.17/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.17/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.17/Heltec_v3_companion_radio_usb_tcp.bin) |
