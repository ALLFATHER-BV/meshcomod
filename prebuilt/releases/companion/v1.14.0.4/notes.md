## v1.14.0.4 — 2026-02-24

**Firmware version:** v1.14.0.4 (meshcomod on upstream 1.14+).

**Highlights:**
- **WiFi WebSocket companion parity:** Contact list over `ws://<device-ip>:8765` now completes reliably. Retries for CONTACT/END frame writes so transient buffer full no longer drops the list; WebSocket/TCP no longer disconnect on first write failure so companion layer can retry. Same protocol as serial/TCP (binary frames, START → N×CONTACT → END).
- All v1.14.0.3 behavior (WebSocket server, Sync-Since) unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.4/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.4/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.4/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.4/Heltec_v3_companion_radio_usb_tcp.bin) |
