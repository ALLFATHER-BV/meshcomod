## v1.14.1.13 — 2026-03-28

**Firmware version:** v1.14.1.13 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights**

- **HTTPS URL OTA:** **`begin(client, fullUrl)`** first (early companion / v1.14.0.x style); if that fails, retry with **host + port + path** from **`meshcoreHttpOtaParseHttpUrl`**. **No `useHTTP10(true)`** — default HTTP/1.1 with **`setReuse(false)`**.
- **Direct flasher first** for pasted **`firmware-download/…`** URLs; **one raw GitHub fallback** via **`meshcoreMeshcomodProxyToRawGithub`**. On fallback after an **HTTP** error (e.g. 404), only **`end`/`stop`** the client; **full TLS rebuild** only when the direct attempt returned **`code < 0`** (TLS/connect).
- **Companion `ota url` over WebSocket/TCP:** **Pinned reply target** for the whole OTA so progress lines still use the correct transport after **`yield()`** during download; **`setReplyTarget(rt)`** before the final **`pushMeshcomodReply`**.
- **meshcomod web app:** clearer Settings → OTA log when the socket closes **before any device OTA output** vs **after** progress (expected during reboot).

**Skip note:** upgrade from **v1.14.1.12** if **`ota url`** over Wi‑Fi showed **WebSocket closed** with **no OTA lines**, or flasher OTA behaved worse than **curl**.

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.14.1.13`, on-device suffix git **`e56a09b9`** (compile-time `git rev-parse --short HEAD` when binaries were built).

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
