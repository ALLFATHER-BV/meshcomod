## v1.14.0.18 — 2026-02-24

**Firmware version:** v1.14.0.18 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS TLS handshake 100ms read timeout:** Use mbedtls_ssl_conf_read_timeout(100) and recv_timeout callback so the handshake can receive data; fixes ERR_CONNECTION_CLOSED when opening https://device:8765 (cert page and wss:// now work).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.18/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.18/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.18/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.18/Heltec_v3_companion_radio_usb_tcp.bin) |
