## v1.14.1.10 — 2026-03-29

**Firmware version:** v1.14.1.10 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights**

- **URL OTA regression fix (match v1.14.0.x):** **`flasher.meshcomod.com`** / **`repeater.meshcomod.com`** `firmware-download/…` links are **no longer rewritten to `raw.githubusercontent.com`** before download. Pre–1.14.1-merge firmware used **`OTA: direct`** to the flasher host; rewriting forced a raw-GitHub fetch that could return **HTTP 404** on-device while the flasher proxy still served **200** (disk). Raw→flasher **fallback** still applies when the user starts from a **raw GitHub** ALLFATHER path.
- **Includes** v1.14.1.8 **WS `pauseListen`**, TLS session **rebuild** on fallback, and **ERR** lines on failure.

**Skip note:** Upgrade from **v1.14.1.9** if **`ota url`** with a **flasher** link ended in **`ERR: HTTP 404`** after a raw-GitHub attempt.

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.14.1.10`.

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
