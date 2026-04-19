## v1.14.0.9 — 2026-02-24

**Firmware version:** v1.14.0.9 (meshcomod on upstream 1.14+).

**Highlights:**
- **GetContacts over WiFi/WebSocket fixed:** WebSocket server-to-client frames with payload length ≥126 bytes now use RFC 6455 extended length encoding (2-byte **big-endian** length). The previous little-endian encoding caused browsers to drop or garble CONTACT (148-byte) and other large frames; START/END (5-byte) and small channel frames (<126) were unaffected.
- v1.14.0.8 behavior unchanged (optional WS frame debug, binary-only stream, reply-target pinning).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.9/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.9/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.9/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.9/Heltec_v3_companion_radio_usb_tcp.bin) |
