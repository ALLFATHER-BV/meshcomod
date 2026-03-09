## v1.14.0.14 — 2026-02-24

**Firmware version:** v1.14.0.14 (meshcomod on upstream 1.14+).

**Highlights:**
- **UI first in loop():** `ui_task.loop()` now runs at the start of every `loop()` so the version screen can dismiss at 3s even if mesh/serial (TCP, USB) blocks later. Fixes device stuck on version screen when `the_mesh.loop()` ran before the UI.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.14/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.14/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.14/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.14/Heltec_v3_companion_radio_usb_tcp.bin) |
