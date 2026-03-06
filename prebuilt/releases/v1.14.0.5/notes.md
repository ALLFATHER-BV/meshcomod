## v1.14.0.5 — 2026-02-24

**Firmware version:** v1.14.0.5 (meshcomod on upstream 1.14+).

**Highlights:**
- **Contact list diagnostic logging (temporary):** Serial output when sending contact list: `contacts: sent START count=... ret=...`, `contacts: sent CONTACT i=... ret=...`, `contacts: sent END ret=...`. Use USB serial at 115200 to verify firmware is sending CONTACT/END; if logs show them but client does not, the issue is on the WebSocket send path.
- **Version and date:** Boot/device info show v1.14.0.5 and build date 24 Feb 2026; fallback in MyMesh.h kept in sync for correct display.
- All v1.14.0.4 behavior (contact retries, WS/TCP no disconnect on first write failure) unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.5/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.5/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.5/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.5/Heltec_v3_companion_radio_usb_tcp.bin) |
