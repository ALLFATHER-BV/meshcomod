## v1.14.0.10 — 2026-02-24

**Firmware version:** v1.14.0.10 (meshcomod on upstream 1.14+).

**Highlights:**
- **Build script improvements:** `build-firmware <target>` no longer wipes `out/`; only bulk builds (build-firmwares, build-companion-firmwares, etc.) clear it, so you can run V4 then V3 and both bins accumulate. Platform detection for ESP32 mergebin/copy fixed: config read via pio pipe (no shell JSON corruption); fallback when platform empty: if `.pio/build/<env>/bootloader.bin` and `partitions.bin` exist, treat as ESP32.
- Same firmware as v1.14.0.9 (plain ws:// WebSocket on port 8765; GetContacts over WiFi fix). WSS (TLS) removed.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.10/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.10/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.10/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.10/Heltec_v3_companion_radio_usb_tcp.bin) |
