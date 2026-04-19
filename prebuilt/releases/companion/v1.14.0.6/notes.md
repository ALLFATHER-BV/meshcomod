## v1.14.0.6 — 2026-03-06

**Firmware version:** v1.14.0.6 (meshcomod on upstream 1.14+).

**Highlights:**
- **GetContacts over WiFi/WebSocket fixed:** Contact list now completes: START (2) → N×CONTACT (3) → END (4). Reply target is saved when sending START and restored before each CONTACT/END so all frames go to the same client (fixes bug where CONTACT/END were sent to USB when USB was polled first).
- getReplyTarget/setReplyTarget in BaseSerialInterface and MultiTransport; MyMesh pins _contact_list_reply_target for the duration of the list.
- Contact list diagnostic logging (Serial) and retries unchanged from v1.14.0.5.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.6/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.6/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.6/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.6/Heltec_v3_companion_radio_usb_tcp.bin) |
