# Room multitransport r1.15.0.4 — 2026-04-21

**Train:** **`r1.15.0.4`**. On-device version string: **`meshcomod-r1.15.0.4-room-mt-<gitsha>`**.

**What this is:** MeshCore **stock chat room** mesh/room logic plus meshcomod **USB + BLE + TCP + WebSocket** (`MultiTransportCompanionInterface`). Stock **`*_room_server`** builds are unchanged.

**Highlights (vs r1.15.0.3):**
- **Xiao S3 WIO support:** Added meshcomod room multitransport prebuilt binaries for **Seeed Xiao S3 WIO SX1262** (`Xiao_S3_WIO_room_server_multitransport`).
- **Release automation:** `scripts/copy-room-release-bins.sh` and `scripts/validate-prebuilt-release-folder.sh` now include Xiao room artifacts.

**Build:** `export ROOM_FIRMWARE_VERSION=r1.15.0.4-room-mt` → `sh build.sh build-room-multitransport-firmwares` → `sh scripts/copy-room-release-bins.sh r1.15.0.4`.

## Prebuilt binaries

| Device | Merged (flash @ 0x0) | App-only |
|--------|----------------------|----------|
| Heltec V4 OLED | [heltec_v4_room_server_multitransport-merged.bin](heltec_v4_room_server_multitransport-merged.bin) | [heltec_v4_room_server_multitransport.bin](heltec_v4_room_server_multitransport.bin) |
| Heltec V4 TFT | [heltec_v4_tft_room_server_multitransport-merged.bin](heltec_v4_tft_room_server_multitransport-merged.bin) | [heltec_v4_tft_room_server_multitransport.bin](heltec_v4_tft_room_server_multitransport.bin) |
| Heltec V3 | [Heltec_v3_room_server_multitransport-merged.bin](Heltec_v3_room_server_multitransport-merged.bin) | [Heltec_v3_room_server_multitransport.bin](Heltec_v3_room_server_multitransport.bin) |
| Heltec Wireless Paper | [Heltec_Wireless_Paper_room_server_multitransport-merged.bin](Heltec_Wireless_Paper_room_server_multitransport-merged.bin) | [Heltec_Wireless_Paper_room_server_multitransport.bin](Heltec_Wireless_Paper_room_server_multitransport.bin) |
| Seeed Xiao S3 WIO SX1262 | [Xiao_S3_WIO_room_server_multitransport-merged.bin](Xiao_S3_WIO_room_server_multitransport-merged.bin) | [Xiao_S3_WIO_room_server_multitransport.bin](Xiao_S3_WIO_room_server_multitransport.bin) |

**Wi‑Fi:** Compile-time **`WIFI_SSID`** / **`WIFI_PWD`** via `scripts/inject_wifi_env.py`, USB **`wifi.*`** commands, or NVS from meshcomod.

**WebSocket:** Plain **`ws://`** on port **8765** (default), same trade-off as companion/repeater for HTTPS OTA RAM.
