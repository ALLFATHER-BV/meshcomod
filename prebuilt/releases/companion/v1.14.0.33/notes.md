## v1.14.0.33 — 2026-03-21

**Firmware version:** v1.14.0.33 (meshcomod on upstream 1.14+).

**Highlights:**
- **Companion ↔ app 1.14 parity:** `CMD_SET_PATH_HASH_MODE` (61), `path_hash_mode` in `CMD_DEVICE_QEURY`, flood sends use `_prefs.path_hash_mode`; `CMD_SET_AUTOADD_CONFIG` / `CMD_GET_AUTOADD_CONFIG` include `autoadd_max_hops` (third byte on set).
- **`/new_prefs` aligned with upstream `main`:** Reserved byte after `path_hash_mode`; `autoadd_max_hops` and `rx_boosted_gain` at end of file (93-byte layout). **One-time migration** from legacy Meshcomod 91-byte files (13-byte tail) on load; rewritten to new format automatically.
- **SX1262/SX1268:** `rx_boosted_gain` in `NodePrefs`; applied at boot and after `CMD_SET_RADIO_PARAMS`; `CustomSX1262Wrapper` / `CustomSX1268Wrapper` expose `setRxBoostedGainMode`.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.33/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.33/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.33/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.33/Heltec_v3_companion_radio_usb_tcp.bin) |
