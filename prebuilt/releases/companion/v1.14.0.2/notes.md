## v1.14.0.2 — 2026-02-25

**Firmware version:** v1.14.0.2 (meshcomod on upstream 1.14+).

**Highlights:**
- **Sync-Since (for future custom client):** New command **62** (SyncSince) with 4-byte LE timestamp payload; firmware responds with message frames (7/8/16/17) since T then **response 61** (SyncSinceDone). Enables backfill after reconnect. Stock clients unchanged; custom client must send **62** (not 60) and handle **61**. See docs/companion_protocol.md and in-code comments.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; just choose a version and flash. For manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.2/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.2/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.2/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.2/Heltec_v3_companion_radio_usb_tcp.bin) |
