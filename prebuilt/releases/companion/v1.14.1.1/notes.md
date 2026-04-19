## v1.14.1.1 — 2026-03-24

**Firmware version:** v1.14.1.1 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights**

- **Heltec V4 TFT + capacitive touch:** `DISPLAY_ROTATION=2` for correct panel orientation on touch builds.
- **Release tooling:** [`AGENTS.md`](../../../AGENTS.md) playbook, [`scripts/validate-prebuilt-release-folder.sh`](../../../scripts/validate-prebuilt-release-folder.sh`), updated companion/repeater release docs and prebuilt READMEs.
- **This folder:** meshcomod USB+TCP companions (V4 OLED, V4 TFT+touch, V3) and Heltec V4 **companion** extras only (USB / BLE / Wi‑Fi for OLED + TFT). **TCP repeater** (`*_repeater_tcp`) latest pin: **[`repeater/r1.14.1.1`](../../repeater/r1.14.1.1/)**; plain mesh repeaters / older TCP rows may still be under **[`repeater/r1.14.1.0`](../../repeater/r1.14.1.0/)**. Rolling [`prebuilt/`](../../) repeater stable names follow the newest **`copy-repeater-release-bins.sh`** run.

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |

**Additional Heltec V4 meshcomod binaries (app / OTA partition)**

| Build | File |
|-------|------|
| OLED companion USB | [heltec_v4_companion_radio_usb.bin](heltec_v4_companion_radio_usb.bin) |
| OLED companion BLE | [heltec_v4_companion_radio_ble.bin](heltec_v4_companion_radio_ble.bin) |
| OLED companion Wi‑Fi | [heltec_v4_companion_radio_wifi.bin](heltec_v4_companion_radio_wifi.bin) |
| TFT companion USB | [heltec_v4_tft_companion_radio_usb.bin](heltec_v4_tft_companion_radio_usb.bin) |
| TFT companion BLE | [heltec_v4_tft_companion_radio_ble.bin](heltec_v4_tft_companion_radio_ble.bin) |
| TFT companion Wi‑Fi | [heltec_v4_tft_companion_radio_wifi.bin](heltec_v4_tft_companion_radio_wifi.bin) |

**Repeaters:** **TCP Wi‑Fi repeater** — **[`repeater/r1.14.1.1`](../../repeater/r1.14.1.1/)**; plain mesh / legacy TCP folder **[`repeater/r1.14.1.0`](../../repeater/r1.14.1.0/)**. **v1.14.1.1** does not ship repeater binaries inside this companion folder.

**Build:** `DISABLE_DEBUG=1`, `FIRMWARE_VERSION=v1.14.1.1`, git **`b96def9b`** (see `out/…-v1.14.1.1-b96def9b…`).

**Procedure:** [`docs/RELEASE_PROCEDURE.md`](../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-release-bins.sh`](../../../scripts/copy-release-bins.sh), [`scripts/copy-heltec-v4-meshcomod-extras.sh`](../../../scripts/copy-heltec-v4-meshcomod-extras.sh). TCP repeater promotion: [`scripts/copy-repeater-release-bins.sh`](../../../scripts/copy-repeater-release-bins.sh) **`r1.14.1.1`** (or **`r1.14.1.0`** / legacy **`v1.14.1.0`** for older pins).
