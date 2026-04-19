## v1.14.0.7 — 2026-03-06

**Firmware version:** v1.14.0.7 (meshcomod on upstream 1.14+).

**Highlights:**
- **Companion stream binary-only:** Removed contact-list Serial.printf debug from the companion path. USB/WS/TCP companion stream now carries only framed protocol bytes (writeFrame*); no ASCII debug mixed in, so parser no longer sees contaminated stream. Fixes GetContacts and Web Serial when same transport was used for protocol + debug.
- Retry logic and reply-target pinning (v1.14.0.6) unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.7/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.7/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.7/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.7/Heltec_v3_companion_radio_usb_tcp.bin) |
