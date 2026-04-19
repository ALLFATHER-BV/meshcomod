## Companion firmware v1.14.0.38

Date: 2026-03-23

### Highlights
- **Companion OTA (~57% flash write):** `ESP32Board::startHttpOtaFromUrl` now uses a **deterministic fetch path** for `ALLFATHER-BV/meshcomod/main/…` URLs: **`https://flasher.meshcomod.com/firmware-download/…` first**, then **one fallback** to **raw GitHub** if flasher HTTPS fails. Removed jsDelivr, repeater, and HTTP proxy mirror hopping.
- **Diagnostics:** After HTTP 200, logs **Content-Length**, **Content-Type**, **first-byte signature**, **effective URL** (tail), and **`max_sketch`** (free OTA slot). On `Update.write` failure, logs **byte offset**, **`Update.getError()`**, **`errorString()`**, and **free sketch space**.
- **Preflight / payload checks:** Rejects **merged** `.bin` URLs for OTA, **HTML** responses (header or `<` body signature), **image larger than OTA partition**, **truncated** streams vs `Content-Length`, and **size mismatch** before verify.

### Root cause (observed failure class)
Mid-download **`OTA: ERR flash write`** with progress stuck around **~57%** was consistent with a **bad or oversized payload** (e.g. HTML/error page, wrong artifact, or length mismatch) while the mirror chain made logs ambiguous. This release **pins the primary download to flasher HTTPS**, validates the body early, and **fails fast** with explicit errors instead of silent bad writes.

### Binaries
- `heltec_v4_companion_radio_usb_tcp.bin`
- `heltec_v4_companion_radio_usb_tcp-merged.bin`
- `heltec_v4_tft_companion_radio_usb_tcp_touch.bin`
- `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin`
- `Heltec_v3_companion_radio_usb_tcp.bin`
- `Heltec_v3_companion_radio_usb_tcp-merged.bin`
