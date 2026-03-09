## v1.14.0.11 — 2026-02-24

**Firmware version:** v1.14.0.11 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS (TLS) tab:** New UI tab after the WiFi/TCP tab shows WSS status: running / not running, port, and client count. When not yet running, explains that WSS starts 10s after WiFi is connected.
- **WSS restored:** Device serves wss:// on port 8765 (default) so https:// clients can connect; non-blocking accept and TLS handshake. Build script and v1.14.0.10 behavior otherwise unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.11/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.11/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.11/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.11/Heltec_v3_companion_radio_usb_tcp.bin) |
