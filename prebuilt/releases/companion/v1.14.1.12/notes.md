## v1.14.1.12 — 2026-03-28

**Firmware version:** v1.14.1.12 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights**

- **HTTPS URL OTA:** OTA uses **`HTTPClient::begin(client, host, port, uri, https)`** instead of **`begin(client, fullUrl)`** so the request line and **`Host:`** header are built from parsed components. This targets **`ERR: HTTP 404`** (or other bogus statuses) on long **`firmware-download/…`** and **raw.githubusercontent.com** URLs while **curl** still shows **200**.
- **Diagnostics:** on non-200 HTTP responses, the device emits **`OTA: HTTP <code> host=… path=…`** so logs show the exact path used.
- **Unchanged:** **`HTTP/1.0`** OTA mode, **direct flasher first**, then **raw GitHub fallback** for meshcomod **`firmware-download/…`** when direct fails.

**Skip note:** upgrade from **v1.14.1.11** if **`ota url`** to a valid flasher or raw URL still fails with **404** or unclear HTTP errors.

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.14.1.12`, git **`f5e419bb`**.

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
