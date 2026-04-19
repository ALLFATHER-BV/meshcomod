## v1.14.1.5 — 2026-03-28

**Firmware version:** v1.14.1.5 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights**

- **WebSocket is plain `ws://` only** on the companion (and repeater TCP) WebSocket port. **WSS** was removed from the device to save flash and **free internal RAM** for **HTTPS URL OTA** (especially important on **Heltec V3** without PSRAM).
- **meshcomod-client** copy was updated so connection hints match stock firmware (`ws://` to the radio; use HTTP-hosted UI when HTTPS blocks mixed content).

**Skip note:** If you never use **Wi‑Fi / WebSocket** or **HTTPS URL OTA** on the companion and are stable on **v1.14.1.4**, you can skip; everyone else should upgrade.

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.14.1.5`.

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
