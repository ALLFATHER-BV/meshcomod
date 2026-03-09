## v1.14.0.13 — 2026-02-24

**Firmware version:** v1.14.0.13 (meshcomod on upstream 1.14+).

**Highlights:**
- **Version screen / TCP defer:** TCP and WSS server start is deferred 5s after boot so the splash can dismiss at 3s and USB is serviced. Fixes device stuck on version screen and GetContacts/GetChannel timeouts over USB. WSS still starts only after 10s + WiFi.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.13/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.13/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.13/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.13/Heltec_v3_companion_radio_usb_tcp.bin) |
