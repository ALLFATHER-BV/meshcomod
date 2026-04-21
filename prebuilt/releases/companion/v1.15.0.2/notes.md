## v1.15.0.2 — 2026-04-21

**Firmware version:** v1.15.0.2 (meshcomod on upstream **MeshCore 1.15.0**).

**Highlights**

- **Xiao S3 WIO SX1262:** New meshcomod companion build **`Xiao_S3_WIO_companion_radio_usb_tcp`** (headless USB + BLE + TCP + WebSocket).
- **Companion release tooling:** `copy-release-bins.sh` / `validate-prebuilt-release-folder.sh` now include Xiao companion artifacts.
- **Full companion USB+TCP matrix:** V4 OLED, V4 TFT+touch, V3, Wireless Paper, and Xiao S3 WIO (merged + app-only).

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |
| Heltec Wireless Paper (E213) | [Heltec_Wireless_Paper_companion_radio_usb_tcp-merged.bin](Heltec_Wireless_Paper_companion_radio_usb_tcp-merged.bin) | [Heltec_Wireless_Paper_companion_radio_usb_tcp.bin](Heltec_Wireless_Paper_companion_radio_usb_tcp.bin) |
| Seeed Xiao S3 WIO SX1262 | [Xiao_S3_WIO_companion_radio_usb_tcp-merged.bin](Xiao_S3_WIO_companion_radio_usb_tcp-merged.bin) | [Xiao_S3_WIO_companion_radio_usb_tcp.bin](Xiao_S3_WIO_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.15.0.2`, git **`aefa9d2e`** (short SHA embedded in `out/` filenames for this promotion).

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
