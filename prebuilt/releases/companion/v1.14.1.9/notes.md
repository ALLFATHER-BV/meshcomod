## v1.14.1.9 — 2026-03-29

**Firmware version:** v1.14.1.9 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights**

- **HTTPS URL OTA (companion RAM):** if you paste a **`flasher.meshcomod.com`** or **`repeater.meshcomod.com`** `firmware-download/…` URL, OTA now hits **that HTTPS URL first** instead of raw GitHub first. Companions often have only **~38KB max contiguous internal heap** for TLS; a **failed raw-GitHub attempt** could leave too little room for the **flasher** fallback, so OTA failed fast and **`restoreAfterHttpOta`** ran (log: **`OTA: resumed WebSocket listen`**) without a visible download. Raw-GitHub-first remains when the URL is **not** a meshcomod flasher/repeater link.
- **Errors:** HTTP OTA failure replies are **emitted as OTA progress lines** so the client shows **`ERR: …`** instead of only a transport restore.
- **Includes** v1.14.1.8 **WebSocket `pauseListen`** during URL OTA when using WS control.

**Skip note:** Upgrade from **v1.14.1.8** if **`ota url`** with a **flasher** link failed right after **`diag try flasher`** or only showed **resumed WebSocket listen**.

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.14.1.9`.

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
