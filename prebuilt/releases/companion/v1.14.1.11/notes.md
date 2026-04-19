## v1.14.1.11 — 2026-03-29

**Firmware version:** v1.14.1.11 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights**

- **HTTPS URL OTA:** `HTTPClient::useHTTP10(true)` for OTA fetches — avoids ESP32 stacks sometimes mis-parsing **HTTP/1.1** responses from nginx (symptom: **`ERR: HTTP 404`** while **curl** shows **200** for the same URL).
- **Direct flasher still first** for `firmware-download/…` URLs; if that fails, **automatic `OTA: fallback raw-github`** to the same object on **raw.githubusercontent.com** (separate TLS session after rebuild).
- **Server ops:** if flasher **HTTPS** keeps failing for devices, confirm **Certbot’s `listen 443 ssl` server block** includes **`meshcomod-firmware-download.conf`** (see `Meshcomod-client/deploy/nginx-meshcomod-flasher.conf` comments).

**Skip note:** upgrade from **v1.14.1.10** if **`OTA: direct`** still ends in **404**.

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.14.1.11`.

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
