# Room multitransport r1.15.0.3 — 2026-04-19

**Train:** **`r1.15.0.3`**. On-device version string: **`meshcomod-r1.15.0.3-room-mt-<gitsha>`**.

**What this is:** MeshCore **stock chat room** mesh/room logic plus meshcomod **USB + BLE + TCP + WebSocket** (`MultiTransportCompanionInterface`). Stock **`*_room_server`** builds are unchanged.

**Highlights (vs r1.15.0.2):**
- **Display:** Multi-page UI (repeater-style) on boards with **`DISPLAY_CLASS`** + user button: **meshcomod** splash, then **Radio** (LoRa), **Network (TCP)** (IP, TCP/WS ports, SSID / Wi‑Fi state), **BLE** (pin, connection / waiting), **WebSocket** (`ws://…` when connected). Page dots; short press = next page, double-click = previous; long press on Network toggles TCP/WS, long press on BLE toggles BLE.
- **Build:** **`MomentaryButton`** linked for all **`_room_server_multitransport`** envs (V4 OLED/TFT, V3, Wireless Paper).

**Unchanged from r1.15.0.2:** USB **`wifi.*`** CLI, NVS **`WifiRuntimeStore`**, protocol surface for framed companion commands.

**Build:** `export ROOM_FIRMWARE_VERSION=r1.15.0.3-room-mt` → `sh build.sh build-room-multitransport-firmwares` → `sh scripts/copy-room-release-bins.sh r1.15.0.3`.

## Prebuilt binaries

| Device | Merged (flash @ 0x0) | App-only |
|--------|----------------------|----------|
| Heltec V4 OLED | [heltec_v4_room_server_multitransport-merged.bin](heltec_v4_room_server_multitransport-merged.bin) | [heltec_v4_room_server_multitransport.bin](heltec_v4_room_server_multitransport.bin) |
| Heltec V4 TFT | [heltec_v4_tft_room_server_multitransport-merged.bin](heltec_v4_tft_room_server_multitransport-merged.bin) | [heltec_v4_tft_room_server_multitransport.bin](heltec_v4_tft_room_server_multitransport.bin) |
| Heltec V3 | [Heltec_v3_room_server_multitransport-merged.bin](Heltec_v3_room_server_multitransport-merged.bin) | [Heltec_v3_room_server_multitransport.bin](Heltec_v3_room_server_multitransport.bin) |
| Heltec Wireless Paper | [Heltec_Wireless_Paper_room_server_multitransport-merged.bin](Heltec_Wireless_Paper_room_server_multitransport-merged.bin) | [Heltec_Wireless_Paper_room_server_multitransport.bin](Heltec_Wireless_Paper_room_server_multitransport.bin) |

**Wi‑Fi:** Compile-time **`WIFI_SSID`** / **`WIFI_PWD`** via `scripts/inject_wifi_env.py`, USB **`wifi.*`** commands, or NVS from meshcomod.

**WebSocket:** Plain **`ws://`** on port **8765** (default), same trade-off as companion/repeater for HTTPS OTA RAM.
