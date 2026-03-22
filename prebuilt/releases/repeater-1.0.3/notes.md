## repeater-1.0.3 — 2026-03-22

**TCP repeater prebuilt** (Heltec WiFi LoRa 32 **V4** + **V3**). Train: **`repeater-X.Y.Z`**, independent of companion **`v1.14.0.x`**.

**Compile-time version string:** `repeater-1.0.3-7acbaea0` (git SHA at link).

**Changes vs repeater-1.0.2**

- **URL OTA / GitHub:** Rewrite **`https://github.com/…/raw/…`** to **`https://raw.githubusercontent.com/…`** before download; **`HTTPClient`** **`HTTPC_FORCE_FOLLOW_REDIRECTS`** + **`User-Agent`** (default was no redirects — **`github.com/raw`** returned **302**, breaking OTA).
- Includes **1.0.2** behaviour: OLED WiFi OTA screen, **`OTA:`** progress on MESHCM **`0x8C`**.
- Built with **`DISABLE_DEBUG=1`**.

**Images (app-only — flash at app offset, typically 0x10000):**

| Device | Binary |
|--------|--------|
| Heltec V4 | [heltec_v4_repeater_tcp.bin](heltec_v4_repeater_tcp.bin) |
| Heltec V3 | [Heltec_v3_repeater_tcp.bin](Heltec_v3_repeater_tcp.bin) |

**Procedure:** [`docs/REPEATER_RELEASE_PROCEDURE.md`](../../../docs/REPEATER_RELEASE_PROCEDURE.md). **Protocol / client:** [`docs/REPEATER_TCP_COMPANION.md`](../../../docs/REPEATER_TCP_COMPANION.md).
