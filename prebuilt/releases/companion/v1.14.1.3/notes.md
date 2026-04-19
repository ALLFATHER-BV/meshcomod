## v1.14.1.3 — 2026-03-26

**Firmware version:** v1.14.1.3 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights**

- **Heltec V4 TFT touch UX pass:** improved touch handling and UI polish on TFT builds:
  - swipe navigation direction fixed (`next` / `prev`)
  - long-press detection made more reliable
  - clearer on-screen hints and larger page indicators
  - slightly longer display timeout for touch usability

**Skip note:** If you run **non-TFT companions** (V4 OLED or V3) and are happy with **v1.14.1.2**, you can skip this release; it is mainly a TFT touch UX refinement.

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.14.1.3`.

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
