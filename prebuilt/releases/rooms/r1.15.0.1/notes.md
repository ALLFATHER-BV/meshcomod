# Room multitransport r1.15.0.1 — 2026-04-19

**Train:** **`r1.15.0.1`** (parallel to companion **`v1.15.0.1`** numeric line). On-device version string: **`meshcomod-r1.15.0.1-room-mt-<gitsha>`**.

**What this is:** MeshCore **stock chat room** mesh/room logic (`simple_room_server` / `MyMesh`) plus meshcomod **USB + BLE + TCP + WebSocket** via `MultiTransportCompanionInterface`. Stock **`*_room_server`** builds are unchanged.

**Protocol surface:** Handshake only (`CMD_DEVICE_QUERY` / `CMD_APP_START` shape); framed sync/contacts/chat client commands return **`RESP_CODE_ERR`** + **`ERR_CODE_UNSUPPORTED_CMD`**.

**Build:** `export ROOM_FIRMWARE_VERSION=r1.15.0.1-room-mt` → `sh build.sh build-room-multitransport-firmwares` → `sh scripts/copy-room-release-bins.sh r1.15.0.1`.

## Prebuilt binaries

| Device | Merged (flash @ 0x0) | App-only |
|--------|----------------------|----------|
| Heltec V4 OLED | [heltec_v4_room_server_multitransport-merged.bin](heltec_v4_room_server_multitransport-merged.bin) | [heltec_v4_room_server_multitransport.bin](heltec_v4_room_server_multitransport.bin) |
| Heltec V4 TFT | [heltec_v4_tft_room_server_multitransport-merged.bin](heltec_v4_tft_room_server_multitransport-merged.bin) | [heltec_v4_tft_room_server_multitransport.bin](heltec_v4_tft_room_server_multitransport.bin) |
| Heltec V3 | [Heltec_v3_room_server_multitransport-merged.bin](Heltec_v3_room_server_multitransport-merged.bin) | [Heltec_v3_room_server_multitransport.bin](Heltec_v3_room_server_multitransport.bin) |
| Heltec Wireless Paper | [Heltec_Wireless_Paper_room_server_multitransport-merged.bin](Heltec_Wireless_Paper_room_server_multitransport-merged.bin) | [Heltec_Wireless_Paper_room_server_multitransport.bin](Heltec_Wireless_Paper_room_server_multitransport.bin) |

**Wi‑Fi:** Build uses `scripts/inject_wifi_env.py`; set **`WIFI_SSID`** / **`WIFI_PWD`** in the environment at compile time, or use on-device Wi‑Fi runtime (NVS) like other meshcomod TCP builds.

**WebSocket:** Plain **`ws://`** on the WebSocket port (default **8765**), same trade-off as companion/repeater TCP for HTTPS OTA RAM.
