## v1.15.0.1 — 2026-04-19

**Firmware version:** v1.15.0.1 (meshcomod on upstream **MeshCore 1.15.0**).

**Highlights**

- **Heltec Wireless Paper (E213):** New meshcomod companion build **`Heltec_Wireless_Paper_companion_radio_usb_tcp`** — USB + BLE + TCP + WebSocket, Wi‑Fi inject + merged flash image (same stack as V3/V4 `*_companion_radio_usb_tcp`). Board profile: **ESP32-S3, 8 MB flash, no PSRAM** (`esp32-s3-devkitc-1`); merged bins use **`--flash_size 8MB`** — confirm against your hardware before full-chip flash.
- **E‑ink UI:** `WIRELESS_PAPER` builds set **`AUTO_OFF_MILLIS=0`** so the ui-new task matches other e‑ink companions (longer msg-preview cadence, slower sensor page polling). **`E213Display`** still skips physical refresh when the frame CRC is unchanged.
- **Bugfix:** Declare **`set_boot_phase`** in Wireless Paper **`target.h`** so companion **`main.cpp`** boot-trace compiles.
- **Companion version bump:** **`MyMesh.h`** default **`FIRMWARE_VERSION`** → **`v1.15.0.1`**.

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |
| Heltec Wireless Paper (E213) | [Heltec_Wireless_Paper_companion_radio_usb_tcp-merged.bin](Heltec_Wireless_Paper_companion_radio_usb_tcp-merged.bin) | [Heltec_Wireless_Paper_companion_radio_usb_tcp.bin](Heltec_Wireless_Paper_companion_radio_usb_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.15.0.1`, git **`cc213eea`** (short SHA at build time).

**TCP repeater:** Promoted in parallel under **[`prebuilt/releases/repeater/r1.15.0.1/`](../../repeater/r1.15.0.1/)** (V4, V3, **Wireless Paper**). See [`notes.md`](../../repeater/r1.15.0.1/notes.md) there.

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh).
