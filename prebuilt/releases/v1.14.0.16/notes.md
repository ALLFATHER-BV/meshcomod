## v1.14.0.16 — 2026-02-24

**Firmware version:** v1.14.0.16 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS TLS handshake non-blocking:** Use NULL recv_timeout in mbedtls_ssl_set_bio so the TLS handshake never blocks the main loop; fixes ERR_CONNECTION_RESET when opening https://deviceIP:8765 in a browser (cert acceptance page now loads).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.16/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.16/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.16/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.16/Heltec_v3_companion_radio_usb_tcp.bin) |
