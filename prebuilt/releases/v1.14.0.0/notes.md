## v1.14.0.0 — 2026-03-06

**Firmware version:** v1.14.0.0 (meshcomod on upstream 1.14+).

**Highlights:**
- Integrate official MeshCore upstream 1.14+ (origin/main) with all Meshcomod companion customizations on top.
- Preserve multi-transport (USB + TCP + BLE), per-client history/sync, BLE queue prioritization, WiFi runtime config and reconnect, V3 stock-parity display, and Meshcomod local command behavior.
- Add upstream 1.14 protocol/persistence: path_hash_mode, autoadd_max_hops in NodePrefs and DataStore; getAutoAddMaxHops() in MyMesh.
- Repeater and room-server build verified; no shared-code regressions.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; just choose a version and flash. For manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.0/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.0/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.0/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.0/Heltec_v3_companion_radio_usb_tcp.bin) |
