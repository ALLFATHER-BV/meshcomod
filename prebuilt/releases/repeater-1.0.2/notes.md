## repeater-1.0.2 — 2026-03-22

**TCP repeater prebuilt** (Heltec WiFi LoRa 32 **V4** + **V3**). Train: **`repeater-X.Y.Z`**, independent of companion **`v1.14.0.x`**.

**Compile-time version string:** `repeater-1.0.2-df50f7a0` (git SHA at link).

**Changes vs repeater-1.0.1**

- **URL OTA feedback:** Heltec OLED full-screen **WiFi OTA** view (status line, progress bar, “do not power off”) during **`ota url`**.
- **MESHCM / repeater.meshcomod.com:** extra **`0x8C`** lines with the same CLI tag, bodies prefixed **`OTA:`** (`connecting`, `downloading N%`, `verifying`, `rebooting`, `ERR …`) so the web client OTA log can show live progress (**`SyncNextMessage`** while OTA runs).
- Built with **`DISABLE_DEBUG=1`**.

**Images (app-only — flash at app offset, typically 0x10000):**

| Device | Binary |
|--------|--------|
| Heltec V4 | [heltec_v4_repeater_tcp.bin](heltec_v4_repeater_tcp.bin) |
| Heltec V3 | [Heltec_v3_repeater_tcp.bin](Heltec_v3_repeater_tcp.bin) |

**Procedure:** [`docs/REPEATER_RELEASE_PROCEDURE.md`](../../../docs/REPEATER_RELEASE_PROCEDURE.md). **Protocol / client:** [`docs/REPEATER_TCP_COMPANION.md`](../../../docs/REPEATER_TCP_COMPANION.md).
