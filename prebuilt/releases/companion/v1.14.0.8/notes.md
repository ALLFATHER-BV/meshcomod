## v1.14.0.8 — 2026-02-24

**Firmware version:** v1.14.0.8 (meshcomod on upstream 1.14+).

**Highlights:**
- **Optional WS frame debug:** Build with `-DWS_FRAME_DEBUG=1` to log each WebSocket send (client id, frame code 2=START/3=CONTACT/4=END, len, written). Use WiFi-only when enabled; see `src/helpers/esp32/DEBUG_WS_FRAME.md` for diagnosing GetContacts over WiFi.
- v1.14.0.7 behavior unchanged (binary-only companion stream, reply-target pinning).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.8/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.8/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.8/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.8/Heltec_v3_companion_radio_usb_tcp.bin) |
