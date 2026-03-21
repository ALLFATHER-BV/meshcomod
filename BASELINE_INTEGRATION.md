# Upstream 1.14+ integration baseline

Branch: `integrate/upstream-main-1.14plus` (created from `origin/main`).
Recovery baseline: `main` at `a70d713`.

## Companion/shared files that differ (main vs origin/main)

- **Meshcomod-only (add from main):** MultiTransportCompanionInterface.cpp/h, TCPCompanionServer.cpp/h, WiFiConfig.cpp/h, build envs heltec_v4_companion_radio_usb_tcp, Heltec_v3_companion_radio_usb_tcp.
- **Keep Meshcomod override (re-apply our logic on upstream 1.14):** MyMesh.cpp, MyMesh.h, main.cpp, SerialBLEInterface.cpp/h, AbstractUITask.h, UITask.cpp, DataStore.cpp, NodePrefs.h, BaseSerialInterface.h.
- **Take upstream 1.14 + minimal Meshcomod tweaks:** Packet, Dispatcher, BaseChatMesh, CommonCLI (protocol/path); variants heltec_v3/target.cpp (V3 stock-parity display), heltec_v4/target.cpp, platformio.ini in variants.

## High-risk merge order

1. Add Meshcomod-only transport and build: MultiTransportCompanionInterface, TCPCompanionServer, WiFiConfig, variant platformio usb_tcp envs, inject_wifi_env if missing.
2. Shared protocol: keep upstream Packet, Dispatcher, BaseChatMesh, CommonCLI; add any Meshcomod-specific overrides if present.
3. Companion app: merge MyMesh, main.cpp, SerialBLEInterface with upstream 1.14; preserve history/sync, writeFrameToAll, BLE priority queue, WiFi reconnect. **Parity:** `CMD_SET_PATH_HASH_MODE` (61), device-info `path_hash_mode` byte, flood `sendFlood` path-hash size from prefs, autoadd get/set including `autoadd_max_hops` (match meshcore-dev companion protocol).
4. Persistence: NodePrefs, DataStore — match upstream `main` tail layout: `path_hash_mode`, reserved byte, `ble_pin`…`autoadd_config`, `autoadd_max_hops`, `rx_boosted_gain`; one-time migration from legacy Meshcomod 91-byte `/new_prefs` (13-byte tail) on load.
5. UI and variants: AbstractUITask, UITask, heltec_v3/v4 target.cpp and platformio.
