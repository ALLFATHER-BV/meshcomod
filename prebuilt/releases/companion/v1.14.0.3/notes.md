## v1.14.0.3 — 2026-02-24

**Firmware version:** v1.14.0.3 (meshcomod on upstream 1.14+).

**Highlights:**
- **WebSocket server for browser-only WiFi:** When TCP is on and WiFi is up, the device also listens for WebSocket on port **8765**. Browser can connect with `ws://<device-ip>:8765` for device + browser only (no bridge). Same companion protocol as TCP; binary frames only. Status shows `ws: 8765` or `ws: off`. See docs/companion_protocol.md § "WiFi for browser only".
- Sync-Since (62/61) and all v1.14.0.2 behavior unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.3/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.3/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.3/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.3/Heltec_v3_companion_radio_usb_tcp.bin) |
