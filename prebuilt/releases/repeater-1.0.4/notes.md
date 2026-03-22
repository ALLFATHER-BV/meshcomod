## repeater-1.0.4 — 2026-03-22

**TCP repeater prebuilt** (Heltec WiFi LoRa 32 **V4** + **V3**). Train: **`repeater-X.Y.Z`**, independent of companion **`v1.14.0.x`**.

**Compile-time version string:** `repeater-1.0.4-cc27eca2` (git SHA at link).

**Changes vs repeater-1.0.3**

- **URL OTA (MESHCM):** Strip trailing **line endings and whitespace** from the HTTPS URL before **`HTTPClient`** (companion sends `ota url …` with a trailing newline; that byte was part of the URL and many servers responded **HTTP 400**).
- Includes **1.0.3** behaviour: GitHub raw rewrite, redirect follow, **`OTA:`** lines on MESHCM **`0x8C`**, OLED WiFi OTA view.

**Images (app-only — flash at app offset, typically 0x10000):**

| Device | Binary |
|--------|--------|
| Heltec V4 | [heltec_v4_repeater_tcp.bin](heltec_v4_repeater_tcp.bin) |
| Heltec V3 | [Heltec_v3_repeater_tcp.bin](Heltec_v3_repeater_tcp.bin) |

**Procedure:** [`docs/REPEATER_RELEASE_PROCEDURE.md`](../../../docs/REPEATER_RELEASE_PROCEDURE.md). **Protocol / client:** [`docs/REPEATER_TCP_COMPANION.md`](../../../docs/REPEATER_TCP_COMPANION.md).
