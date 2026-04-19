## v1.15.0.0 — 2026-04-19

**Firmware version:** v1.15.0.0 (meshcomod on upstream **MeshCore 1.15.0**).

**Highlights**

- **Upstream 1.15.0:** Brings stock MeshCore 1.15.0 changes (default scope, `GROUP_DATA` path, companion/repeater fixes, new board support upstream, etc.) while retaining meshcomod add-ons.
- **meshcomod preserved:** Multi-transport companion (USB + BLE + TCP/WS), Meshcomod local command contact, `CMD_SYNC_SINCE` / `RESP_CODE_SYNC_SINCE_DONE`, path-hash mode command, prefs migration + new default-scope fields.
- **Channel datagram vs sync:** Command id `62` is shared upstream (`CMD_SEND_CHANNEL_DATA`) vs meshcomod `CMD_SYNC_SINCE`; firmware disambiguates by payload layout (see `MyMesh.cpp`).

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.15.0.0`, on-device suffix git **`ecd15291`** (compile-time `git rev-parse --short HEAD` when binaries were built).

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
