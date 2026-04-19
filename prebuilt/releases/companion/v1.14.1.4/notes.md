## v1.14.1.4 — 2026-03-26

**Firmware version:** v1.14.1.4 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights**

- **Heltec V4 TFT touch UI overhaul:** cleaner page hierarchy (title + status strip), broader palette accents, and touch-first interaction polish.
- **Battery indicator redesign:** materially smaller and lower visual weight on TFT.
- **Action reliability fixes:** GPS hold toggle now responds reliably and advert action shows explicit sent/failed feedback.

**Skip note:** If you run **non-TFT companions** (V4 OLED or V3) and are happy with **v1.14.1.3**, you can skip this release; it primarily targets the TFT touch experience.

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.14.1.4`.

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
