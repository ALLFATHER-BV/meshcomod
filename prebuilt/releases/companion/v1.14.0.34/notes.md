## v1.14.0.34 — 2026-03-22

**Firmware version:** v1.14.0.34-b70b1934 (meshcomod on upstream 1.14+).

**Highlights:**
- **meshcore-cli (TCP): no duplicate lines per message.** After a successful broadcast push of `CONTACT_MSG_RECV_V3` / `CHANNEL_MSG_RECV_V3`, the companion advances all clients’ sync history watermarks (`advanceAllHistoryClientsToSeq`) so `CMD_SYNC_NEXT_MESSAGE` does not replay the same frame you already got on the live push. Guarded when multi-transport broadcast is off (`companionUnsolicitedPushesBroadcastToAll` / `_broadcast`) so secondary clients are not skipped for sync.
- **`addToHistoryRing`** returns the assigned sequence for callers (used by the above).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |
