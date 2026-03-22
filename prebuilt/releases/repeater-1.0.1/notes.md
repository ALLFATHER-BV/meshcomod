## repeater-1.0.1 — 2026-03-22

**TCP repeater prebuilt** (Heltec WiFi LoRa 32 **V4** + **V3**). Train: **`repeater-X.Y.Z`**, independent of companion **`v1.14.0.x`**.

**Compile-time version string:** `repeater-1.0.1-70d9c52b` (git SHA at link).

**Changes vs repeater-1.0.0**

- **`ota url <https://…>`** — while Wi‑Fi STA is up, download an **app-only** `.bin` from allowlisted HTTPS URLs (GitHub raw / meshcomod **`firmware-download`** proxy), flash the running app partition, deferred reboot. Aligns with **repeater.meshcomod.com** / flasher manifest resolution.
- Built with **`DISABLE_DEBUG=1`** (release logging profile).

**Images (app-only — flash at app offset, typically 0x10000):**

| Device | Binary |
|--------|--------|
| Heltec V4 | [heltec_v4_repeater_tcp.bin](heltec_v4_repeater_tcp.bin) |
| Heltec V3 | [Heltec_v3_repeater_tcp.bin](Heltec_v3_repeater_tcp.bin) |

**Procedure:** [`docs/REPEATER_RELEASE_PROCEDURE.md`](../../../docs/REPEATER_RELEASE_PROCEDURE.md). **Protocol / client:** [`docs/REPEATER_TCP_COMPANION.md`](../../../docs/REPEATER_TCP_COMPANION.md).
