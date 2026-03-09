## v1.14.0.12 — 2026-02-24

**Firmware version:** v1.14.0.12 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS boot fix:** mbedTLS is no longer initialized in the WebSocket server constructor (at global init). All TLS init is deferred to `begin()`, which runs only after the 10s defer and WiFi connected. Fixes device stuck on "Loading..." so the splash and home screen appear normally.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.12/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.12/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.12/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.12/Heltec_v3_companion_radio_usb_tcp.bin) |
