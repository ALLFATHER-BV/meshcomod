# Room server multitransport (template)

Meshcomod **USB + BLE + TCP + WebSocket** builds for the stock MeshCore **chat room** firmware (`*_room_server_multitransport`). Stock **`*_room_server`** envs are unchanged.

## Build / copy

From repo **`MeshCore/`**:

```bash
export ROOM_FIRMWARE_VERSION=r1.15.0.0-room-mt
sh build.sh build-room-multitransport-firmwares
sh scripts/copy-room-release-bins.sh r1.15.0.0
```

## Binaries (when promoted)

- **Heltec V4 OLED:** `heltec_v4_room_server_multitransport.bin`, optional `heltec_v4_room_server_multitransport-merged.bin`
- **Heltec V4 TFT:** `heltec_v4_tft_room_server_multitransport.bin`, optional merged
- **Heltec V3:** `Heltec_v3_room_server_multitransport.bin`, optional merged
- **Heltec Wireless Paper:** `Heltec_Wireless_Paper_room_server_multitransport.bin`, optional merged

## Protocol

Handshakes only (`CMD_DEVICE_QUERY` / `CMD_APP_START`); sync/contacts/chat framed commands return **`RESP_CODE_ERR`** with **`ERR_CODE_UNSUPPORTED_CMD`**.

## Validate

```bash
sh scripts/validate-prebuilt-release-folder.sh rooms r1.15.0.0
```
