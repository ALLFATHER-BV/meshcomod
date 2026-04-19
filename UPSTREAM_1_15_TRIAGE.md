# MeshCore 1.15 Upstream Triage (meshcomod)

Branch: `integrate/upstream-1.15.0`  
Upstream base: `origin/main` @ `dee3e26` (`companion-v1.15.0` / `repeater-v1.15.0`)  
Fork source: `allfather/main` @ `1bab3d78`

## Triage Method

1. Start from upstream 1.15.
2. Identify meshcomod fork delta against upstream.
3. Classify overlap files as:
   - **Take upstream** when change is stock-safe and meshcomod has no additive logic.
   - **Adapt/merge** when both sides changed same path.
   - **Take meshcomod** for meshcomod-only transport/repeater/TCP/WS features.
4. Validate by compiling meshcomod target firmwares and running meshcomod-client protocol tests.

## Key Decisions

### Take meshcomod (custom feature paths)

- Multi-transport companion stack (`src/helpers/esp32/MultiTransportCompanionInterface.*`).
- TCP/WS companion server paths (`src/helpers/esp32/TCPCompanionServer.*`, `WebSocketCompanionServer.*`, `SerialWifiInterface.*`).
- WiFi runtime / OTA helper files and repeater TCP integration files.
- Heltec v3/v4 meshcomod variant target files and build scripts for meshcomod release flow.

### Adapt/merge (high-risk overlap)

- `examples/companion_radio/MyMesh.cpp` / `MyMesh.h`
  - Preserved meshcomod local command handling and `CMD_SYNC_SINCE`.
  - Kept upstream 1.15-compatible default scope support hooks where possible.
  - Kept channel data handling reachable while avoiding collision with sync-since via payload-length branching.
- `examples/companion_radio/DataStore.cpp`
  - Preserved legacy meshcomod prefs-tail migration.
  - Preserved upstream-tail compatibility and added `default_scope_name` / `default_scope_key` persistence.
- `src/helpers/CommonCLI.*`
  - Preserved meshcomod HTTP OTA wifi-path argument support in `handleCommand(...)`.
- `examples/simple_repeater/UITask.cpp` / `MyMesh.h`
  - Preserved repeater tcp/ui customizations and button handling compatibility.

### Take upstream

- Upstream core 1.15 updates outside meshcomod override surface (already present by branching from `origin/main`).

## Validation Performed

### Firmware builds (success)

- `FIRMWARE_VERSION=v1.15.0-meshcomod sh build.sh build-firmware heltec_v4_companion_radio_usb_tcp`
- `FIRMWARE_VERSION=v1.15.0-meshcomod sh build.sh build-firmware Heltec_v3_companion_radio_usb_tcp`
- `FIRMWARE_VERSION=v1.15.0-meshcomod sh build.sh build-firmware heltec_v4_tft_companion_radio_usb_tcp_touch`

### Client protocol tests (success)

In `Meshcomod-client`:

- `npm run test -- src/protocol/companion_serial.test.ts src/core/session_manager.test.ts src/firmware_catalog_repeater_versions.test.ts`
- Result: 3 files passed, 72 tests passed.

## Follow-ups Before Promotion

1. Hardware smoke tests:
   - serial + tcp/ws connect
   - sync since behavior for `mccli` / `meshcomod`
   - path hash mode set/get
   - repeater wifi + stats flow
2. Choose final release version string (replace `v1.15.0-meshcomod` fallback as needed).
3. Generate/prep release artifacts and notes per release procedure.
