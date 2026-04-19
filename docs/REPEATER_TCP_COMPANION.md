# Repeater TCP companion protocol

Repeater firmware can expose the **same binary framing** as the USB/BLE/TCP companion: client sends frames prefixed with `<` (0x3C), length as two little-endian bytes, then payload; the device responds with `>` (0x3E), same length header, then payload. This matches [`TCPCompanionServer`](../src/helpers/esp32/TCPCompanionServer.h) and **[`WebSocketCompanionServer`](../src/helpers/esp32/WebSocketCompanionServer.h)** (**plain `ws://`** on port **8765** by default; meshcomod does not ship WSS on the device), and the meshcomod / MeshCore companion wire format (see project docs such as `COMPANION_SERIAL_PROTOCOL` / meshcomod `companion_serial.ts`).

**Custom client / meshcomod UI:** see **[`REPEATER_MESHCOMOD_CLIENT_INTEGRATION.md`](REPEATER_MESHCOMOD_CLIENT_INTEGRATION.md)** for TCP+WS parity, MESHCM Wi‑Fi CLI, `CMD_GET_STATS` byte layouts, and a suggested repeater settings screen checklist.

Firmware environments:

- `heltec_v4_repeater_tcp` — Heltec WiFi LoRa 32 V4 (OLED repeater + TCP).
- `Heltec_v3_repeater_tcp` — Heltec V3 variant (parity).

They **do not** compile `examples/companion_radio/`; command handling is a **subset** implemented on `simple_repeater` [`MyMesh`](../examples/simple_repeater/MyMesh.cpp) when `REPEATER_TCP_COMPANION` is set, plus [`examples/repeater_tcp/main.cpp`](../examples/repeater_tcp/main.cpp) for WiFi, **TCP + WebSocket** accept/poll, and USB serial.

## Configuration

- **`TCP_PORT`** — defaults to `5000` in the PlatformIO env (raw TCP companion protocol).
- **`WS_PORT`** — defaults to `8765`. Repeater and companion builds use **plain `ws://` only** (see [`DEVICE_WEBSOCKET_WIFI.md`](DEVICE_WEBSOCKET_WIFI.md) for HTTPS vs mixed content).

WebSocket listening starts only after **TCP companion is enabled** and **WiFi is connected** (socket is stopped whenever TCP companion transport is off, or before WiFi has an IP). **`tickHandshake()`** runs every loop while the WebSocket server is up so accepts and upgrades are processed promptly.

### Wi-Fi (same NVS model as companion)

Credentials are stored in ESP32 **Preferences** namespace `meshcomod` (keys `wifi_ssid` / `wifi_pwd`) via [`WifiRuntimeStore`](../src/helpers/esp32/WifiRuntimeStore.cpp) — the **same storage** as companion firmware, so you can move a board between images without re-entering SSID if NVS is kept.

**Primary (runtime):**

1. **USB serial** (repeater text CLI): `set wifi.ssid MyNet`, `set wifi.pwd secret`, **`set wifi.apply`** (meshcomod uses this form), or `wifi.apply` / `set wifi.clear` / `wifi.clear`, plus `get wifi.ssid`, `get wifi.status` / `wifi.status` (includes `radio_enabled=0|1`), **`set wifi.radio 1`** / **`set wifi.radio 0`** to turn the Wi‑Fi interface on or off (saved in NVS; off powers down STA and drops LAN until turned back on) — same idea as companion debug `set` / `get` lines.
2. **meshcomod over TCP or WebSocket**: same binary protocol on **TCP** (`5000`) or **WS** (`8765`); use **`ws://<device-ip>:8765/`**.

**Optional compile-time defaults:** uncomment `WIFI_SSID` / `WIFI_PWD` in the `*_repeater_tcp` env in `platformio.ini` if you want a factory fallback when NVS is empty.

Example build:

```bash
cd MeshCore
python3 -m platformio run -e heltec_v4_repeater_tcp
```

**Flash image:** use the **app-only** `firmware.bin` (PlatformIO: `.pio/build/heltec_v4_repeater_tcp/firmware.bin`, or `out/heltec_v4_repeater_tcp.bin` after a plain `pio run`). Flash at the **app partition offset** (typically **0x10000** on these ESP32-S3 images), not at 0x0, unless you also flash bootloader + partition table separately. A single **merged** file for 0x0 is optional: `python3 -m platformio run -e heltec_v4_repeater_tcp -t mergebin` → `.pio/build/heltec_v4_repeater_tcp/firmware-merged.bin` (not copied to `out/` by default).

**OTA (over-the-air) updates:** the `*_repeater_tcp` envs already pull in **`esp32_ota`** (AsyncElegantOTA + ESPAsyncWebServer), same as the plain Heltec repeater.

1. **`start ota`** (USB serial or MESHCM **`TXT_TYPE_CLI_DATA`**) — open AP **`MeshCore-OTA`**, HTTP **`/update`** upload (same as other ESP32 repeaters). Use a second device on that AP; not on your LAN STA at the same time.

2. **`ota url <https://…>`** — while **Wi‑Fi STA is connected**, the device **GETs** an **app-only** `.bin` from an **allowlisted** HTTPS URL, streams it into the running app partition, then reboots. Allowed hosts/paths include **`https://raw.githubusercontent.com/…`**, **`https://github.com/…/raw/…`**, and meshcomod **`/firmware-download/`** on **`flasher.meshcomod.com`** or **`repeater.meshcomod.com`** (same nginx proxy as the web flasher). **`https://github.com/…/raw/…`** is rewritten internally to **`https://raw.githubusercontent.com/…`** before the request (GitHub issues **302** for the short URL; HTTPClient defaults to **not** following redirects). Redirect follow is also set to **force** for any remaining hops. Example: `ota url https://github.com/ALLFATHER-BV/meshcomod/raw/main/prebuilt/heltec_v4_repeater_tcp.bin`. **Final** reply: **`> OK rebooting`** (same **`0x8C`** tag as other MESHCM CLI lines; sent after progress). **Merged** full-flash images are not supported on this path—use USB + flasher if partitions/bootloader mismatch. TLS uses **insecure verify** today (`setInsecure()`); prefer pinned URLs you trust. **Companion builds** (`*companion_radio_usb_tcp*`): for **`ALLFATHER-BV/meshcomod/main/…`** raw paths, the firmware uses **`https://flasher.meshcomod.com/firmware-download/…`** as the **primary** GET; if that fails it retries **raw GitHub only** (no jsDelivr, repeater, or HTTP mirror hopping).

   **Companion OTA memory note:** If companion HTTPS OTA fails before `HTTP OK` with `HTTP -1` or `tls=... SSL - Memory allocation failed`, see [`COMPANION_OTA_MEMORY_ROOT_CAUSE.md`](COMPANION_OTA_MEMORY_ROOT_CAUSE.md). The confirmed root cause was internal-RAM pressure from live companion transports, especially BLE; fully releasing BLE before OTA made companion HTTPS OTA succeed.

   **Progress (MESHCM / repeater.meshcomod.com):** During the download, the firmware may push **additional** **`0x8C`** frames with the **same 4-byte tag** as the pending CLI reply. Bodies are UTF-8 lines, typically prefixed with **`OTA:`** (e.g. `OTA: connecting`, `OTA: HTTP OK, flashing`, `OTA: downloading 42%`, `OTA: verifying`, `OTA: rebooting`, or `OTA: ERR …` on failure). The web client should **`SyncNextMessage`** while OTA runs (as for Debug CLI) to drain these. **Heltec OLED (`SSD1306`):** While URL OTA is active, the display switches to a full-screen **WiFi OTA** view: status line, progress bar (percent when `Content-Length` is known, else an indeterminate animation), and **do not power off**.

**Flasher / release bundles:** repeater TCP uses release id **`r1.14.1.x`** under **`prebuilt/releases/repeater/`** (parallel to companion **`v1.14.1.x`**) plus **`‑repeater-tcp`** in **`REPEATER_FIRMWARE_VERSION`**. **`build.sh`** adds the **`meshcomod-`** prefix on **`*_repeater_tcp`** targets. Build with **`REPEATER_FIRMWARE_VERSION=r1.14.1.0-repeater-tcp`**, then **`scripts/copy-repeater-release-bins.sh r1.14.1.0`** — binaries land under **`prebuilt/`** and **`prebuilt/releases/repeater/r1.14.1.0/`**. Legacy **`v1.14.1.0-repeater-tcp`** in **`out/`** still works with **`copy-repeater-release-bins.sh v1.14.1.0`** (destination **`repeater/r1.14.1.0/`**). The copy script accepts **`repeater-X.Y.Z`** → **`prebuilt/releases/repeater/repeater-X.Y.Z/`** in forks. See [`REPEATER_RELEASE_PROCEDURE.md`](REPEATER_RELEASE_PROCEDURE.md), [`prebuilt/README.md`](../prebuilt/README.md), [`WHERE_IS_REPEATER_FIRMWARE.md`](../WHERE_IS_REPEATER_FIRMWARE.md).

### OLED UI (Heltec V4 `heltec_v4_repeater_tcp`)

Heltec V3 **`Heltec_v3_repeater_tcp`** uses the same OLED behaviour when **`DISPLAY_CLASS`** is set.

**Boot splash** (~3 s, same spirit as companion **ui-new**): **`meshcomod`** (large), firmware version (trailing **`-<gitsha>`** hidden on screen if it looks like a short hex SHA), **`FIRMWARE_BUILD_DATE`**, then **Repeater**. After that, the multi-page UI runs as before.

After the boot splash, the user button matches the **companion-style** flow where possible:

- **Short press** — next page (four pages: radio summary, **network** status, **WebSocket** URL/status, advert). Page changes are ignored during the **boot splash** (~3 s) so presses don’t skip past the network screen unseen.
- **Double-click** — previous page (also ignored during ~3 s boot splash).
- **Long press (first ~8 s after boot)** — USB CLI banner (WiFi `set` commands hint), like companion “rescue” timing.
- **Long press on network page** (after splash, after rescue window) — toggle **TCP + WebSocket** off/on together (`repeater_transport_enabled`).
- **Long press on advert page** — send **flood** advert (short press only advances to the next page, same as companion-style paging).

## Supported commands (subset)

| Cmd | Name | Behaviour |
|-----|------|-----------|
| 22 | Device query | `RESP` 13 device info; repeater reports **0** contacts / **0** channels, **0** BLE PIN, **0** `client_repeat`; `path_hash_mode` from prefs. **Firmware string** is **40 bytes** at offset **60** (total payload **102** bytes); **`REPEATER_COMPANION_FIRMWARE_VER_CODE`** **27**+ (meshcomod clients: `parseDeviceInfoBinaryV10` when `payload.length >= 102`). Older repeaters used **20** bytes / **82** bytes total. |
| 1 | App start | `RESP` 5 self info; advert type **repeater** (`ADV_TYPE_REPEATER`). |
| 2 | Send text (Meshcomod) | To pubkey prefix **`MESHCM`**: **`TXT_TYPE_PLAIN`** → **wifi** / **help** / **status** only; replies `SENT`, `CONFIRMED`, `CONTACT_MSG_RECV_V3`, `MSG_WAITING`. **`TXT_TYPE_CLI_DATA`** → **same CLI as USB** (`get`/`set advert.interval`, `get`/`set flood.advert.interval`, etc.); replies `SENT` then **`PUSH_CODE_BINARY_RESPONSE` (0x8C)** = `[8C][0][tag 4 LE][UTF-8 output]` + `MSG_WAITING` (no code 16). Other destinations → `ERR` unsupported. |
| 4 | Get contacts | `RESP` 2 with count **0** (meshcomod resolves immediately). |
| 10 | Sync next message | `RESP` 10 no more messages. |
| 62 | Sync since | `RESP` 61 done (no history stream). |
| 5 / 6 | Get / set device time | Same as companion. |
| 8 | Set advert name | Updates prefs + save. |
| 14 | Set advert lat/lon | Updates `sensors` + prefs. |
| 7 | Send self advert | Zero-hop or flood (byte 2 optional: `1` = flood). |
| 11 / 12 | Set radio params / TX power | Applies `radio_set_*` + save prefs. |
| 21 / 43 | Set / get tuning params | `rx_delay_base`, `airtime_factor`. |
| 38 | Set other params | Updates `advert_loc_policy`, `multi_acks` when bytes present (manual/telemetry bytes ignored). |
| 61 | Set path hash mode | Same rules as companion (`[61, 0, mode]`). |
| 19 | Reboot | Payload `reboot` (6 chars after command byte). |
| 20 | Battery + storage | Millivolts + SPIFFS used/total (KiB). |
| 56 | Get stats | Subtypes 0 / 1 / 2 — core / radio / packets (layout matches companion). |
| 23 / 24 | Export / import private key | Honours `ENABLE_PRIVATE_KEY_*` like base firmware. |

Any other command returns **`RESP` 1** with **`ERR_CODE_UNSUPPORTED_CMD` (1)** (or another error code where validation fails). **Factory reset** (51) is explicitly unsupported on this image.

## Security

TCP exposes **remote** control comparable to an open serial console: radio settings, identity export/import (if enabled), reboot, time, etc. Use only on **trusted networks**; prefer VLAN/firewall rules, strong WiFi, and non-default passwords. This is **not** authenticated companion transport by itself.

## Source layout

- [`examples/repeater_tcp/repeater_companion_proto.h`](../examples/repeater_tcp/repeater_companion_proto.h) — shared command/response constants.
- [`examples/repeater_tcp/main.cpp`](../examples/repeater_tcp/main.cpp) — WiFi, `TCPCompanionServer`, frame loop.
- `MyMesh::handleRepeaterTcpCompanionCommand` — command implementation (ESP32 + `REPEATER_TCP_COMPANION` only).
