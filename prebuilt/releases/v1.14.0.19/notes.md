## v1.14.0.19 — 2026-02-24

**Firmware version:** v1.14.0.19 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS custom non-blocking recv/send:** TLS handshake uses wss_net_recv_nonblock and wss_net_send_nonblock that return WANT_READ/WANT_WRITE on EAGAIN; no blocking, no wrong error. Fixes ERR_CONNECTION_RESET for https://device:8765 and wss://.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.19/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.19/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.19/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.19/Heltec_v3_companion_radio_usb_tcp.bin) |
