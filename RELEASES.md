# Release log

Versioned prebuilts are listed here so you can **roll back** if a newer release causes issues. The **latest** build is always in [`prebuilt/`](prebuilt/) (same files are updated on each release). For a specific version, use the links below.

## Release process (do this for every new fix/release)

**Rule: Every new fix or release gets a new version number. Never overwrite an existing version's binaries.**

1. **Bump version** — Choose the next version (e.g. `v1.14.0.1`). Do not reuse the current version.
2. **Build** — `export FIRMWARE_VERSION=v1.14.0.1` then build **Heltec V4 OLED**, **V4 TFT+touch** (expansion kit), **V3**, and **Heltec Wireless Paper** (`Heltec_Wireless_Paper_companion_radio_usb_tcp`) meshcomod companions per [`docs/RELEASE_PROCEDURE.md`](docs/RELEASE_PROCEDURE.md) §2.
3. **Create versioned folder** — Run `sh scripts/copy-release-bins.sh v1.14.0.1` to populate `prebuilt/releases/companion/<version>/` and `prebuilt/`. Add `notes.md` in that folder (often done in step 4). Repeater drops use **`prebuilt/releases/repeater/r*`** and `copy-repeater-release-bins.sh`. **Room multitransport** drops use **`prebuilt/releases/rooms/r*`** and `copy-room-release-bins.sh` (see [`prebuilt/README.md`](prebuilt/README.md)).
4. **Update this file** — Add a new section above with the new version number, date, highlights, and table linking to the new release paths.
5. **Commit and push** — Always commit and push: stage version bump, prebuilts, `prebuilt/releases/companion/<version>/` (and repeater folder if applicable), and RELEASES.md; commit with a short message (e.g. `v1.14.0.N: <highlights>`); push to the release remote (e.g. `git push allfather main`). Do not skip this step.

**Summary:** `prebuilt/` = latest only. `prebuilt/releases/companion/v*…/`, `prebuilt/releases/repeater/r*…/`, and `prebuilt/releases/rooms/r*…/` hold pinned drops (bins + `notes.md`); keep all of them so users can roll back.

**WebSocket:** meshcomod **companion** and **repeater TCP** firmware serves **plain `ws://` only** on the WebSocket port (default 8765). **WSS** (`wss://`) was **removed from the device** to reduce flash use and free internal RAM for **HTTPS URL OTA**. Use `ws://` to the radio, or the **HTTP**-hosted web app when HTTPS blocks mixed content — see [`docs/DEVICE_WEBSOCKET_WIFI.md`](docs/DEVICE_WEBSOCKET_WIFI.md).

---

## v1.15.0.1 — 2026-04-19

**Firmware version:** v1.15.0.1 (meshcomod on upstream **MeshCore 1.15.0**).

**Highlights:**
- **Heltec Wireless Paper:** First meshcomod **USB + BLE + TCP + WebSocket** companion prebuilts (`Heltec_Wireless_Paper_companion_radio_usb_tcp`). E‑ink-friendly **`AUTO_OFF_MILLIS=0`** on the Paper variant; **`set_boot_phase`** declared in **`target.h`** for companion boot trace.
- **Repeater TCP:** New **`r1.15.0.1`** drop adds **Wireless Paper** repeater TCP bins alongside refreshed V4/V3 — see [`prebuilt/releases/repeater/r1.15.0.1/notes.md`](prebuilt/releases/repeater/r1.15.0.1/notes.md).
- **Room multitransport:** First pinned **`r1.15.0.1`** meshcomod **chat room** images with USB+BLE+TCP+WS — [`prebuilt/releases/rooms/r1.15.0.1/notes.md`](prebuilt/releases/rooms/r1.15.0.1/notes.md).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.15.0.1/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.15.0.1/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.15.0.1/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.15.0.1/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.15.0.1/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.15.0.1/Heltec_v3_companion_radio_usb_tcp.bin) |
| Heltec Wireless Paper (E213) | [Heltec_Wireless_Paper_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.15.0.1/Heltec_Wireless_Paper_companion_radio_usb_tcp-merged.bin) | [Heltec_Wireless_Paper_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.15.0.1/Heltec_Wireless_Paper_companion_radio_usb_tcp.bin) |

**TCP repeater (superseded for “latest” rolling `prebuilt/`):** folder **[`r1.15.0.1`](prebuilt/releases/repeater/r1.15.0.1/)** — [notes](prebuilt/releases/repeater/r1.15.0.1/notes.md). **Current repeater TCP pin:** **[`r1.15.0.2`](prebuilt/releases/repeater/r1.15.0.2/)** below.

**Room multitransport (superseded for rolling `prebuilt/`):** **[`r1.15.0.1`](prebuilt/releases/rooms/r1.15.0.1/)** — [notes](prebuilt/releases/rooms/r1.15.0.1/notes.md). **Current room multitransport pin:** **[`r1.15.0.4`](prebuilt/releases/rooms/r1.15.0.4/)** below.

**More details:** [`prebuilt/releases/companion/v1.15.0.1/notes.md`](prebuilt/releases/companion/v1.15.0.1/notes.md).

---

## Repeater TCP r1.15.0.2 — 2026-04-19

**Release id:** **`r1.15.0.2`** — Heltec **Wi‑Fi / TCP+WebSocket** repeater images (`*_repeater_tcp`) on the **1.15** version line, including **V4 TFT** and **Wireless Paper** in the promoted set. Rolling [`prebuilt/`](prebuilt/) repeater stable names match this copy.

**Highlights:**
- **Version defaults:** PlatformIO fallbacks use **`v1.15.0.2-repeater-tcp`** (and **`v1.15.0.0-repeater`** / **`v1.15.0.0-repeater-tft`** for plain mesh V4 repeaters).
- **Paper in the matrix:** **`Heltec_Wireless_Paper_repeater_tcp`** is built with **`build-repeater-firmwares`** and copied by **`copy-repeater-release-bins.sh`**.

**Prebuilt binaries:**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_repeater_tcp-merged.bin](prebuilt/releases/repeater/r1.15.0.2/heltec_v4_repeater_tcp-merged.bin) | [heltec_v4_repeater_tcp.bin](prebuilt/releases/repeater/r1.15.0.2/heltec_v4_repeater_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_repeater_tcp-merged.bin](prebuilt/releases/repeater/r1.15.0.2/heltec_v4_tft_repeater_tcp-merged.bin) | [heltec_v4_tft_repeater_tcp.bin](prebuilt/releases/repeater/r1.15.0.2/heltec_v4_tft_repeater_tcp.bin) |
| Heltec V3 | [Heltec_v3_repeater_tcp-merged.bin](prebuilt/releases/repeater/r1.15.0.2/Heltec_v3_repeater_tcp-merged.bin) | [Heltec_v3_repeater_tcp.bin](prebuilt/releases/repeater/r1.15.0.2/Heltec_v3_repeater_tcp.bin) |
| Heltec Wireless Paper (E213) | [Heltec_Wireless_Paper_repeater_tcp-merged.bin](prebuilt/releases/repeater/r1.15.0.2/Heltec_Wireless_Paper_repeater_tcp-merged.bin) | [Heltec_Wireless_Paper_repeater_tcp.bin](prebuilt/releases/repeater/r1.15.0.2/Heltec_Wireless_Paper_repeater_tcp.bin) |

**More details:** [`prebuilt/releases/repeater/r1.15.0.2/notes.md`](prebuilt/releases/repeater/r1.15.0.2/notes.md).

---

## Room multitransport r1.15.0.4 — 2026-04-21

**Release id:** **`r1.15.0.4`** — meshcomod **chat room server** with **USB + BLE + TCP + WebSocket** (`*_room_server_multitransport`). Rolling [`prebuilt/`](prebuilt/) room stable names match this copy.

**Highlights:**
- **Xiao S3 WIO SX1262 added:** New room multitransport binaries for **`Xiao_S3_WIO_room_server_multitransport`**.
- **Release tooling updated:** room copy/validate scripts now include Xiao room bins.

**Prebuilt binaries:**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.4/heltec_v4_room_server_multitransport-merged.bin) | [heltec_v4_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.4/heltec_v4_room_server_multitransport.bin) |
| Heltec V4 TFT | [heltec_v4_tft_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.4/heltec_v4_tft_room_server_multitransport-merged.bin) | [heltec_v4_tft_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.4/heltec_v4_tft_room_server_multitransport.bin) |
| Heltec V3 | [Heltec_v3_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.4/Heltec_v3_room_server_multitransport-merged.bin) | [Heltec_v3_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.4/Heltec_v3_room_server_multitransport.bin) |
| Heltec Wireless Paper (E213) | [Heltec_Wireless_Paper_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.4/Heltec_Wireless_Paper_room_server_multitransport-merged.bin) | [Heltec_Wireless_Paper_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.4/Heltec_Wireless_Paper_room_server_multitransport.bin) |
| Seeed Xiao S3 WIO SX1262 | [Xiao_S3_WIO_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.4/Xiao_S3_WIO_room_server_multitransport-merged.bin) | [Xiao_S3_WIO_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.4/Xiao_S3_WIO_room_server_multitransport.bin) |

**More details:** [`prebuilt/releases/rooms/r1.15.0.4/notes.md`](prebuilt/releases/rooms/r1.15.0.4/notes.md).

---

## Room multitransport r1.15.0.3 — 2026-04-19

**Release id:** **`r1.15.0.3`** — meshcomod **chat room server** with **USB + BLE + TCP + WebSocket** (`*_room_server_multitransport`). Rolling [`prebuilt/`](prebuilt/) room stable names match this copy.

**Highlights:**
- **OLED / TFT / E213 UI:** Repeater-style **meshcomod** splash, then tabbed **Radio** (LoRa), **Network** (IP, TCP/WS ports, SSID / Wi‑Fi state), **BLE** (pin + connection), **WebSocket** (`ws://…` when Wi‑Fi has an address). Short press / double-click change page; long press on Network toggles TCP/WS, long press on BLE toggles BLE. **`MomentaryButton`** added to all **`_room_server_multitransport`** PlatformIO envs.

**Prebuilt binaries:**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.3/heltec_v4_room_server_multitransport-merged.bin) | [heltec_v4_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.3/heltec_v4_room_server_multitransport.bin) |
| Heltec V4 TFT | [heltec_v4_tft_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.3/heltec_v4_tft_room_server_multitransport-merged.bin) | [heltec_v4_tft_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.3/heltec_v4_tft_room_server_multitransport.bin) |
| Heltec V3 | [Heltec_v3_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.3/Heltec_v3_room_server_multitransport-merged.bin) | [Heltec_v3_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.3/Heltec_v3_room_server_multitransport.bin) |
| Heltec Wireless Paper (E213) | [Heltec_Wireless_Paper_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.3/Heltec_Wireless_Paper_room_server_multitransport-merged.bin) | [Heltec_Wireless_Paper_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.3/Heltec_Wireless_Paper_room_server_multitransport.bin) |

**More details:** [`prebuilt/releases/rooms/r1.15.0.3/notes.md`](prebuilt/releases/rooms/r1.15.0.3/notes.md).

---

## Room multitransport r1.15.0.2 — 2026-04-19

**Release id:** **`r1.15.0.2`** — meshcomod **chat room server** with **USB + BLE + TCP + WebSocket** (`*_room_server_multitransport`). Rolling [`prebuilt/`](prebuilt/) room stable names match this copy.

**Highlights:**
- **USB Wi‑Fi configuration** on serial, same commands as repeater/companion: `set wifi.ssid`, `set wifi.pwd`, `wifi.apply`, `wifi.clear`, `get wifi.ssid`, `set wifi.radio`, `wifi.status` / `get wifi.status` (NVS **`WifiRuntimeStore`** + `room_mt_on_wifi_radio_toggled()` for immediate STA when the radio is enabled).

**Prebuilt binaries:**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.2/heltec_v4_room_server_multitransport-merged.bin) | [heltec_v4_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.2/heltec_v4_room_server_multitransport.bin) |
| Heltec V4 TFT | [heltec_v4_tft_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.2/heltec_v4_tft_room_server_multitransport-merged.bin) | [heltec_v4_tft_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.2/heltec_v4_tft_room_server_multitransport.bin) |
| Heltec V3 | [Heltec_v3_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.2/Heltec_v3_room_server_multitransport-merged.bin) | [Heltec_v3_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.2/Heltec_v3_room_server_multitransport.bin) |
| Heltec Wireless Paper (E213) | [Heltec_Wireless_Paper_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.2/Heltec_Wireless_Paper_room_server_multitransport-merged.bin) | [Heltec_Wireless_Paper_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.2/Heltec_Wireless_Paper_room_server_multitransport.bin) |

**More details:** [`prebuilt/releases/rooms/r1.15.0.2/notes.md`](prebuilt/releases/rooms/r1.15.0.2/notes.md).

---

## Room multitransport r1.15.0.1 — 2026-04-19

**Release id:** **`r1.15.0.1`** — meshcomod **chat room server** with **USB + BLE + TCP + WebSocket** (`*_room_server_multitransport`). Stock **`*_room_server`** firmware is a separate build track.

**Highlights:**
- **Heltec V4 OLED / V4 TFT / V3 / Wireless Paper** in one matrix; merged + app-only bins under [`prebuilt/releases/rooms/r1.15.0.1/`](prebuilt/releases/rooms/r1.15.0.1/).
- **Rolling `prebuilt/`** stable names updated by **`scripts/copy-room-release-bins.sh`** (same pattern as repeater companion copy).

**Prebuilt binaries:**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.1/heltec_v4_room_server_multitransport-merged.bin) | [heltec_v4_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.1/heltec_v4_room_server_multitransport.bin) |
| Heltec V4 TFT | [heltec_v4_tft_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.1/heltec_v4_tft_room_server_multitransport-merged.bin) | [heltec_v4_tft_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.1/heltec_v4_tft_room_server_multitransport.bin) |
| Heltec V3 | [Heltec_v3_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.1/Heltec_v3_room_server_multitransport-merged.bin) | [Heltec_v3_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.1/Heltec_v3_room_server_multitransport.bin) |
| Heltec Wireless Paper (E213) | [Heltec_Wireless_Paper_room_server_multitransport-merged.bin](prebuilt/releases/rooms/r1.15.0.1/Heltec_Wireless_Paper_room_server_multitransport-merged.bin) | [Heltec_Wireless_Paper_room_server_multitransport.bin](prebuilt/releases/rooms/r1.15.0.1/Heltec_Wireless_Paper_room_server_multitransport.bin) |

**More details:** [`prebuilt/releases/rooms/r1.15.0.1/notes.md`](prebuilt/releases/rooms/r1.15.0.1/notes.md).

---

## v1.15.0.0 — 2026-04-19

**Firmware version:** v1.15.0.0 (meshcomod on upstream **MeshCore 1.15.0**).

**Highlights:**
- **Upstream 1.15.0:** Merges meshcomod customizations onto stock MeshCore companion **1.15.0** (see [MeshCore companion-v1.15.0](https://github.com/meshcore-dev/MeshCore/releases/tag/companion-v1.15.0) / [release notes](https://blog.meshcore.io/2026/04/19/release-1-15-0)).
- **meshcomod stack retained:** USB + Bluetooth + TCP/WebSocket companion, Meshcomod local command contact, `CMD_SYNC_SINCE`, path-hash mode, multi-client history behavior, and legacy prefs migration (including new default-scope prefs fields).
- **Repeater TCP:** This companion release does not bump repeater TCP bins; existing `prebuilt/releases/repeater/r*` drops remain until a separate repeater promotion.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.15.0.0/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.15.0.0/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.15.0.0/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.15.0.0/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.15.0.0/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.15.0.0/Heltec_v3_companion_radio_usb_tcp.bin) |

**More details:** [`prebuilt/releases/companion/v1.15.0.0/notes.md`](prebuilt/releases/companion/v1.15.0.0/notes.md).

---

## v1.14.1.15 — 2026-03-31

**Firmware version:** v1.14.1.15 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **Stock MeshCore chat fix:** post-live V3 sync-watermark advance is now client-aware; stock/unknown clients keep sync replay available instead of being forced to no-replay mode.
- **meshcomod compatibility preserved:** clients identified via `CMD_APP_START` as `mccli` / `meshcomod` continue using live-advance behavior to avoid duplicate sync replay.
- **Legacy protocol safety kept:** clients negotiating protocol `< 3` remain on sync-adapted replay path.
- Includes prior frame hardening from `v1.14.1.14` (bounded fixed-field string serialization and safe channel index fallback).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.15/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.15/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.15/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.15/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.15/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.15/Heltec_v3_companion_radio_usb_tcp.bin) |

**More details:** [`prebuilt/releases/companion/v1.14.1.15/notes.md`](prebuilt/releases/companion/v1.14.1.15/notes.md).

---

## v1.14.1.14 — 2026-03-31

**Firmware version:** v1.14.1.14 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **Stock MeshCore compatibility hardening:** fixed-width channel/device-info frame fields now use bounded zero-padded serialization (no unsafe `strcpy` into protocol frame slices).
- **Channel message robustness:** companion clamps invalid channel index lookups to a safe in-range value before emitting channel message frames, avoiding client parser/state failures on malformed indexes.
- **Compatibility retained for meshcomod:** selective sync watermark behavior for legacy clients remains intact while modern clients keep duplicate-avoidance behavior.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.14/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.14/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.14/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.14/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.14/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.14/Heltec_v3_companion_radio_usb_tcp.bin) |

**More details:** [`prebuilt/releases/companion/v1.14.1.14/notes.md`](prebuilt/releases/companion/v1.14.1.14/notes.md).

---

## v1.14.1.13 — 2026-03-28

**Firmware version:** v1.14.1.13 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **URL OTA (ESP32):** **`HTTPClient::begin(client, fullUrl)`** first (v1.14.0.x-style), with **parsed host/port/uri** only as a fallback if full-URL `begin` fails; **HTTP/1.0** disabled for OTA (default **HTTP/1.1** + **`Connection: close`** via **`setReuse(false)`**).
- **Meshcomod `firmware-download/…`:** **raw GitHub fallback** after a failed direct fetch **rebuilds TLS only on connection/TLS errors** (`code < 0`); on HTTP errors, **light `end`/`stop`** only before raw retry.
- **Companion progress over Wi‑Fi:** **pins the WS/TCP reply target** for the whole `ota url` flow so **`meshcoreRepeaterTcpOtaEmitLine`** lines still route correctly after **`yield()`** in the download loop; final **`pushMeshcomodReply`** uses the same target.
- **meshcomod web client:** WebSocket/disconnect OTA hints distinguish **“no device output yet”** vs **“link dropped during/after OTA”**.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.13/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.13/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.13/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.13/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.13/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.13/Heltec_v3_companion_radio_usb_tcp.bin) |

**More details:** [`prebuilt/releases/companion/v1.14.1.13/notes.md`](prebuilt/releases/companion/v1.14.1.13/notes.md).

---

## v1.14.1.12 — 2026-03-28

**Firmware version:** v1.14.1.12 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **URL OTA:** **`HTTPClient::begin(client, host, port, uri, https)`** for OTA (explicit **Host** + path) instead of **`begin(client, fullUrl)`** — targets bogus **404** / bad status on flasher and raw GitHub URLs from the device stack.
- **Diagnostics:** **`OTA: HTTP <code> host=… path=…`** on non-200 responses.
- **Companion prebuilts:** [`prebuilt/releases/companion/v1.14.1.12/`](prebuilt/releases/companion/v1.14.1.12/).
- **Skip note:** upgrade from **v1.14.1.11** if **`ota url`** still fails for URLs that work in **curl**.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.12/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.12/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.12/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.12/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.12/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.12/Heltec_v3_companion_radio_usb_tcp.bin) |

**More details:** [`prebuilt/releases/companion/v1.14.1.12/notes.md`](prebuilt/releases/companion/v1.14.1.12/notes.md).

---

## v1.14.1.11 — 2026-03-29

**Firmware version:** v1.14.1.11 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **URL OTA:** **`HTTP/1.0`** for OTA HTTP(S) on ESP32 (more reliable status parsing vs some nginx **HTTP/1.1** responses); **raw GitHub fallback** after a failed **direct** `firmware-download/…` fetch.
- **Companion prebuilts:** [`prebuilt/releases/companion/v1.14.1.11/`](prebuilt/releases/companion/v1.14.1.11/).
- **Skip note:** upgrade from **v1.14.1.10** if **`OTA: direct`** still reports **HTTP 404** for a valid flasher URL.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.11/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.11/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.11/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.11/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.11/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.11/Heltec_v3_companion_radio_usb_tcp.bin) |

**More details:** [`prebuilt/releases/companion/v1.14.1.11/notes.md`](prebuilt/releases/companion/v1.14.1.11/notes.md).

---

## v1.14.1.10 — 2026-03-29

**Firmware version:** v1.14.1.10 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **URL OTA:** **`firmware-download`** URLs on **flasher/repeater.meshcomod.com** are fetched **directly** again (like **v1.14.0.x**), not rewritten to **raw.githubusercontent.com** first — fixes **`ERR: HTTP 404`** on flasher OTA while the proxy still serves the file.
- **Companion prebuilts:** [`prebuilt/releases/companion/v1.14.1.10/`](prebuilt/releases/companion/v1.14.1.10/).
- **Skip note:** upgrade from **v1.14.1.9** if flasher **`ota url`** failed with **404**.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.10/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.10/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.10/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.10/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.10/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.10/Heltec_v3_companion_radio_usb_tcp.bin) |

**More details:** [`prebuilt/releases/companion/v1.14.1.10/notes.md`](prebuilt/releases/companion/v1.14.1.10/notes.md).

---

## v1.14.1.9 — 2026-03-29

**Firmware version:** v1.14.1.9 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **URL OTA + low internal RAM:** for **`flasher.meshcomod.com`** / **`repeater.meshcomod.com`** `firmware-download/…` URLs, OTA fetches **flasher HTTPS first** (raw GitHub second). Avoids a companion TLS failure + **`OTA: resumed WebSocket listen`** with no download when **max alloc heap** is tight (~38KB).
- **OTA errors:** failure replies are **mirrored into OTA progress lines** (`ERR: …`) so the client shows the reason.
- **Companion prebuilts:** [`prebuilt/releases/companion/v1.14.1.9/`](prebuilt/releases/companion/v1.14.1.9/).
- **Skip note:** upgrade from **v1.14.1.8** if flasher URL OTA over WS failed silently or only restored WS listen.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.9/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.9/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.9/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.9/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.9/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.9/Heltec_v3_companion_radio_usb_tcp.bin) |

**More details:** [`prebuilt/releases/companion/v1.14.1.9/notes.md`](prebuilt/releases/companion/v1.14.1.9/notes.md).

---

## v1.14.1.8 — 2026-03-29

**Firmware version:** v1.14.1.8 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **URL OTA from WebSocket:** **pauses WebSocket listen only** during HTTPS fetch (existing WS client stays connected); avoids **device freeze** on **flasher** fallback after raw GitHub. Repeater TCP companion: same when OTA is started over WS.
- **Companion prebuilts:** new pinned folder [`prebuilt/releases/companion/v1.14.1.8/`](prebuilt/releases/companion/v1.14.1.8/) with updated primary USB+TCP binaries.
- **Skip note:** upgrade if **`ota url` over WebSocket** hung after **`OTA: diag try flasher.meshcomod.com`** on **v1.14.1.7** or earlier.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.8/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.8/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.8/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.8/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.8/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.8/Heltec_v3_companion_radio_usb_tcp.bin) |

**More details:** [`prebuilt/releases/companion/v1.14.1.8/notes.md`](prebuilt/releases/companion/v1.14.1.8/notes.md).

---

## v1.14.1.7 — 2026-03-29

**Firmware version:** v1.14.1.7 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **URL OTA + meshcomod client:** again **preserves the active Wi‑Fi control path** (TCP *or* WebSocket) during HTTPS OTA so the **client stays connected** for progress and the final message before reboot — fixes the **immediate disconnect** regression in **v1.14.1.6**.
- **HTTPS:** still uses a **fresh TLS/HTTP session** before the **flasher** fallback after raw GitHub fails (carried over from v1.14.1.6).
- **Companion prebuilts:** new pinned folder [`prebuilt/releases/companion/v1.14.1.7/`](prebuilt/releases/companion/v1.14.1.7/) with updated primary USB+TCP binaries.
- **Skip note:** upgrade from **v1.14.1.6** if Start OTA drops the client; otherwise optional if you are already happy on **v1.14.1.5** or earlier.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.7/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.7/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.7/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.7/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.7/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.7/Heltec_v3_companion_radio_usb_tcp.bin) |

**More details:** [`prebuilt/releases/companion/v1.14.1.7/notes.md`](prebuilt/releases/companion/v1.14.1.7/notes.md).

---

## v1.14.1.6 — 2026-03-29

**Firmware version:** v1.14.1.6 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **HTTPS URL OTA:** companion **suspends TCP and WebSocket** during the HTTPS fetch (not just one transport) so TLS no longer contends with the Wi‑Fi listener; **fresh TLS session** before the flasher fallback; **OTA lines mirrored on USB Serial** during the download.
- **Repeater TCP companion:** same suspend-both-transports behavior for URL OTA.
- **Companion prebuilts:** new pinned folder [`prebuilt/releases/companion/v1.14.1.6/`](prebuilt/releases/companion/v1.14.1.6/) with updated primary USB+TCP binaries.
- **Skip note:** if you never use **HTTPS URL OTA** over Wi‑Fi and are stable on **v1.14.1.5**, you can skip.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.6/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.6/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.6/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.6/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.6/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.6/Heltec_v3_companion_radio_usb_tcp.bin) |

**More details:** [`prebuilt/releases/companion/v1.14.1.6/notes.md`](prebuilt/releases/companion/v1.14.1.6/notes.md).

---

## v1.14.1.5 — 2026-03-28

**Firmware version:** v1.14.1.5 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **WebSocket:** companion (and repeater TCP) serves **plain `ws://` only**; **WSS** removed on-device to reduce flash use and free **internal RAM** for **HTTPS URL OTA** (especially **Heltec V3**).
- **meshcomod-client:** WebSocket transport hints updated to match stock firmware.
- **Companion prebuilts:** new pinned folder [`prebuilt/releases/companion/v1.14.1.5/`](prebuilt/releases/companion/v1.14.1.5/) with updated primary USB+TCP binaries.
- **Skip note:** if you never use Wi‑Fi/WebSocket or HTTPS URL OTA and are stable on **v1.14.1.4**, you can skip.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.5/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.5/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.5/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.5/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.5/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.5/Heltec_v3_companion_radio_usb_tcp.bin) |

**More details:** [`prebuilt/releases/companion/v1.14.1.5/notes.md`](prebuilt/releases/companion/v1.14.1.5/notes.md).

---

## v1.14.1.4 — 2026-03-26

**Firmware version:** v1.14.1.4 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **TFT touch UI overhaul:** improved page hierarchy with explicit page titles, broader accent color usage, and touch-first UX cleanup in `ui-new`.
- **Battery indicator refinement:** reduced to a much smaller, lower-visual-weight icon on Heltec V4 TFT.
- **Action reliability:** GPS toggle now responds reliably on hold and advert action shows clear sent/failed feedback.
- **Companion prebuilts:** new pinned folder [`prebuilt/releases/companion/v1.14.1.4/`](prebuilt/releases/companion/v1.14.1.4/) with updated primary USB+TCP binaries.
- **Skip note:** if you run **non-TFT companions** (V4 OLED / V3) and are already stable on **v1.14.1.3**, you can skip this release.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.4/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.4/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.4/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.4/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.4/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.4/Heltec_v3_companion_radio_usb_tcp.bin) |

**More details:** [`prebuilt/releases/companion/v1.14.1.4/notes.md`](prebuilt/releases/companion/v1.14.1.4/notes.md).

---

## v1.14.1.3 — 2026-03-26

**Firmware version:** v1.14.1.3 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **TFT touch UX pass:** fixed swipe direction mapping, more reliable long-press handling, clearer touch hints, and improved page indicators on Heltec V4 TFT touch.
- **Companion prebuilts:** new pinned folder [`prebuilt/releases/companion/v1.14.1.3/`](prebuilt/releases/companion/v1.14.1.3/) with updated primary USB+TCP binaries.
- **Skip note:** if you run **non-TFT companions** (V4 OLED / V3) and are already stable on **v1.14.1.2**, you can skip this release.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.3/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.3/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.3/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.3/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.3/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.3/Heltec_v3_companion_radio_usb_tcp.bin) |

**More details:** [`prebuilt/releases/companion/v1.14.1.3/notes.md`](prebuilt/releases/companion/v1.14.1.3/notes.md).

---

## v1.14.1.2 — 2026-03-26

**Firmware version:** v1.14.1.2 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **Heltec V4 TFT geometry fix:** V4 TFT base now uses portrait logical mapping (`DISPLAY_ROTATION=2` with swapped width/height scaling) so the UI is no longer too wide/short after rotation updates.
- **Companion prebuilts:** new pinned folder [`prebuilt/releases/companion/v1.14.1.2/`](prebuilt/releases/companion/v1.14.1.2/) with updated primary USB+TCP binaries.
- **Repeater train unchanged:** current TCP repeater pin remains [`prebuilt/releases/repeater/r1.14.1.1/`](prebuilt/releases/repeater/r1.14.1.1/).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.2/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.2/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.2/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.2/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.2/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.2/Heltec_v3_companion_radio_usb_tcp.bin) |

**More details:** [`prebuilt/releases/companion/v1.14.1.2/notes.md`](prebuilt/releases/companion/v1.14.1.2/notes.md).

---

## v1.14.1.1 — 2026-03-24

**Firmware version:** v1.14.1.1 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **Heltec V4 TFT touch:** `DISPLAY_ROTATION=2` in variant config for correct display orientation.
- **Docs / process:** [`AGENTS.md`](AGENTS.md) prebuilt playbook, `validate-prebuilt-release-folder.sh`, companion/repeater release doc updates.
- **TCP repeater (Wi‑Fi):** new pin **[`prebuilt/releases/repeater/r1.14.1.1/`](prebuilt/releases/repeater/r1.14.1.1/)** — SNTP after Wi‑Fi connect (UTC), meshcomod **repeater.meshcomod.com** UI (browser clock sync, locale time, decimal lat/lon + map). On-device string **`meshcomod-r1.14.1.1-repeater-tcp-<sha>`**. Plain mesh repeaters / older TCP row: **[`prebuilt/releases/repeater/r1.14.1.0/`](prebuilt/releases/repeater/r1.14.1.0/)** until extras copy refreshes them.
- **Prebuilts:** [`prebuilt/releases/companion/v1.14.1.1/`](prebuilt/releases/companion/v1.14.1.1/) — USB+TCP companions and Heltec V4 extras only. Rolling [`prebuilt/`](prebuilt/) repeater stable names (`heltec_v4_repeater_tcp.bin`, etc.) match the latest **`copy-repeater-release-bins.sh`** run (target **`r1.14.1.1`** for current TCP repeater).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.1/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.1/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.1/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.1/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.1/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.1/Heltec_v3_companion_radio_usb_tcp.bin) |

**TCP repeater** (pin **`r1.14.1.1`** — current meshcomod TCP repeater):

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_repeater_tcp-merged.bin](prebuilt/releases/repeater/r1.14.1.1/heltec_v4_repeater_tcp-merged.bin) | [heltec_v4_repeater_tcp.bin](prebuilt/releases/repeater/r1.14.1.1/heltec_v4_repeater_tcp.bin) |
| Heltec V4 TFT | [heltec_v4_tft_repeater_tcp-merged.bin](prebuilt/releases/repeater/r1.14.1.1/heltec_v4_tft_repeater_tcp-merged.bin) | [heltec_v4_tft_repeater_tcp.bin](prebuilt/releases/repeater/r1.14.1.1/heltec_v4_tft_repeater_tcp.bin) |
| Heltec V3 | [Heltec_v3_repeater_tcp-merged.bin](prebuilt/releases/repeater/r1.14.1.1/Heltec_v3_repeater_tcp-merged.bin) | [Heltec_v3_repeater_tcp.bin](prebuilt/releases/repeater/r1.14.1.1/Heltec_v3_repeater_tcp.bin) |

Older TCP / plain repeater pins: [`prebuilt/releases/repeater/r1.14.1.0/`](prebuilt/releases/repeater/r1.14.1.0/). Details: [`prebuilt/releases/repeater/r1.14.1.1/notes.md`](prebuilt/releases/repeater/r1.14.1.1/notes.md).

**More Heltec V4 meshcomod images** (USB/BLE/Wi‑Fi companions in **v1.14.1.1**): see [`prebuilt/releases/companion/v1.14.1.1/notes.md`](prebuilt/releases/companion/v1.14.1.1/notes.md).

---

## v1.14.1.0 — 2026-03-23

**Firmware version:** v1.14.1.0 (meshcomod on upstream **MeshCore 1.14.1**).

**Highlights:**
- **Upstream MeshCore 1.14.1** merged (`467959cc`): dispatcher / duty-cycle, SX126x RX boosted gain plumbing, repeater/room/sensor fixes, new hardware variants and docs — see [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore) release tags.
- **Meshcomod unchanged in intent:** USB + BLE + TCP (+ WebSocket) companion, WiFi runtime, **Wi‑Fi‑only HTTP OTA** with minimal mode + restore on failure, repeater TCP companion path, Heltec V4 extras and prebuilt layout.
- **Prebuilts:** [`prebuilt/releases/companion/v1.14.1.0/`](prebuilt/releases/companion/v1.14.1.0/) (companions + USB/BLE/Wi‑Fi extras) and [`prebuilt/releases/repeater/r1.14.1.0/`](prebuilt/releases/repeater/r1.14.1.0/) (plain + TCP repeaters), plus refreshed [`prebuilt/`](prebuilt/). TCP repeater images use version string **`meshcomod-v1.14.1.0-repeater-tcp-<sha>`** on device. **Flasher / Repeater app** pinned downloads use release folder **`r1.14.1.0`** under `repeater/` (see meshcomod-client `firmware_catalog.ts`).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.0/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.0/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.1.0/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.1.0/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.1.0/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.1.0/Heltec_v3_companion_radio_usb_tcp.bin) |

**TCP repeater** (folder **`repeater/r1.14.1.0/`**; version string **`meshcomod-v1.14.1.0-repeater-tcp-<sha>`**):

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_repeater_tcp-merged.bin](prebuilt/releases/repeater/r1.14.1.0/heltec_v4_repeater_tcp-merged.bin) | [heltec_v4_repeater_tcp.bin](prebuilt/releases/repeater/r1.14.1.0/heltec_v4_repeater_tcp.bin) |
| Heltec V4 TFT | [heltec_v4_tft_repeater_tcp-merged.bin](prebuilt/releases/repeater/r1.14.1.0/heltec_v4_tft_repeater_tcp-merged.bin) | [heltec_v4_tft_repeater_tcp.bin](prebuilt/releases/repeater/r1.14.1.0/heltec_v4_tft_repeater_tcp.bin) |
| Heltec V3 | [Heltec_v3_repeater_tcp-merged.bin](prebuilt/releases/repeater/r1.14.1.0/Heltec_v3_repeater_tcp-merged.bin) | [Heltec_v3_repeater_tcp.bin](prebuilt/releases/repeater/r1.14.1.0/Heltec_v3_repeater_tcp.bin) |

**More Heltec V4 meshcomod images** (USB/BLE/Wi‑Fi companions in **companion/**; plain + TCP repeaters in **repeater/r1.14.1.0/**): see [`prebuilt/releases/companion/v1.14.1.0/notes.md`](prebuilt/releases/companion/v1.14.1.0/notes.md) and [`prebuilt/releases/repeater/r1.14.1.0/notes.md`](prebuilt/releases/repeater/r1.14.1.0/notes.md).

---

## v1.14.0.41 — 2026-03-23

**Firmware version:** v1.14.0.41 (meshcomod on upstream 1.14+).

**Highlights:**
- **Wi‑Fi‑only HTTP OTA:** Companion and repeater TCP accept `ota url` only from an active **Wi‑Fi** control session (multi‑transport TCP/WS, or SerialWifi with STA + client). USB/BLE initiation returns a clear `ERR:`.
- **OTA minimal mode:** Before HTTPS OTA, non‑essential services/transports are suspended (e.g. other TCP/WS listener, BLE stack) while keeping the active Wi‑Fi path for progress lines; **automatic restore on failure**; success path reboots without restore.
- **Prebuilts:** Standard **USB+TCP** meshcomod images (V4 OLED, V4 TFT+touch, V3) plus **extra** Heltec V4 OLED/TFT companions (USB, BLE, Wi‑Fi) under [`prebuilt/releases/companion/v1.14.0.41/`](prebuilt/releases/companion/v1.14.0.41/); **plain mesh repeaters** from that build live under [`prebuilt/releases/repeater/r1.14.0.41/`](prebuilt/releases/repeater/r1.14.0.41/). Refreshed [`prebuilt/`](prebuilt/) rolls forward. **TCP repeater** for this era used legacy **`repeater-1.0.10/`** (removed from tree; see **git history**). Current TCP repeater drops live under **`repeater/r1.14.1.0/`** (see newer release above).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.41/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.41/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.0.41/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.0.41/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.41/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.41/Heltec_v3_companion_radio_usb_tcp.bin) |

**More Heltec V4 meshcomod images** (USB/BLE/Wi‑Fi companions; plain repeaters in **repeater/r1.14.0.41/**): see [`prebuilt/releases/companion/v1.14.0.41/notes.md`](prebuilt/releases/companion/v1.14.0.41/notes.md) and [`prebuilt/releases/repeater/r1.14.0.41/notes.md`](prebuilt/releases/repeater/r1.14.0.41/notes.md).

---

## v1.14.0.40 — 2026-03-23

**Firmware version:** v1.14.0.40 (meshcomod on upstream 1.14+).

**Highlights:**
- **Companion OTA now works under real transport load:** before HTTPS OTA starts, companion fully releases BLE when the active session is not BLE, reclaiming the internal RAM needed for mbedTLS on ESP32.
- **Confirmed root cause:** added OTA diagnostics for `tls=` errors plus `heap` / `max` / `psram` / `pmax`, which showed companion HTTPS OTA was failing from **internal-RAM allocation pressure**, not bad URLs or network refusal.
- **Lower TLS memory pressure:** companion builds now use asymmetric mbedTLS content lengths so outgoing TLS buffers cost less RAM while keeping the incoming path compatible with firmware downloads.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.40/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.40/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.0.40/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.0.40/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.40/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.40/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.39 — 2026-03-23

**Firmware version:** v1.14.0.39 (meshcomod on upstream 1.14+).

**Highlights:**
- **Companion OTA GitHub-first:** companion URL OTA now resolves meshcomod `firmware-download` links back to the matching `raw.githubusercontent.com/ALLFATHER-BV/meshcomod/main/...` path and tries **raw GitHub first**.
- **Flasher remains fallback only:** if direct GitHub fetch fails, companion OTA falls back once to **`https://flasher.meshcomod.com/firmware-download/...`**; this keeps OTA working for companions even when the UI sends a flasher URL.
- **Target parity:** V4 OLED, V4 TFT+touch, and V3 companion binaries (merged + non-merged).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.39/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.39/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.0.39/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.0.39/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.39/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.39/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.38 — 2026-03-23

**Firmware version:** v1.14.0.38 (meshcomod on upstream 1.14+).

**Highlights:**
- **Companion OTA stability:** deterministic **`flasher.meshcomod.com` HTTPS** fetch for meshcomod main firmware paths, with **raw GitHub fallback only** (no jsDelivr / repeater / HTTP mirror chain).
- **57% class failures:** preflight **OTA partition size**, reject **HTML** and **merged** images, detect **truncation / size mismatch**; richer **`OTA:`** logs (URL tail, `Content-Type`, `Content-Length`, firmware signature, `Update` error + offset on flash write failure).
- **Target parity:** V4 OLED, V4 TFT+touch, and V3 companion binaries (merged + non-merged).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.38/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.38/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.0.38/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.0.38/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.38/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.38/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.37 — 2026-03-23

**Firmware version:** v1.14.0.37 (meshcomod on upstream 1.14+).

**Highlights:**
- **Release line reset:** removed failed release artifacts after `v1.14.0.36` and restarted clean from `v1.14.0.37`.
- **Companion OTA rollback to baseline path:** restored `ESP32Board` OTA transport logic to the pre-regression baseline used before the mirror-chain churn. **Superseded for OTA behaviour by v1.14.0.38** (deterministic flasher path + stricter validation).
- **Target parity:** V4 OLED, V4 TFT+touch, and V3 companion binaries (merged + non-merged).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.37/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.37/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.0.37/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.0.37/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.37/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.37/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.36 — 2026-03-23

**Firmware version:** v1.14.0.36 (meshcomod on upstream 1.14+).

**Highlights:**
- **Companion OTA updates (V4/V3):** Regular Heltec V4/V3 companion builds now support OTA update flow with the same command model as repeater.
- **Meshcomod OTA commands:** Companion now supports `ota start`, `ota url <https://...bin>`, and `ota status` in the local Meshcomod command contact.
- **Target parity:** OTA-enabled companion release provided for all three companion targets: V4 OLED, V4 TFT+touch, and V3 (app + merged images).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.36/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.36/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.0.36/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.0.36/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.36/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.36/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.35 — 2026-03-22

**Firmware version:** v1.14.0.35 (meshcomod on upstream 1.14+).

**Highlights:**
- **Heltec V4 TFT + capacitive touch:** New env `heltec_v4_tft_companion_radio_usb_tcp_touch` — same meshcomod multi-transport companion stack as OLED V4 (WiFi inject at build, USB/TCP/WebSocket/BLE, merged flash image). ST7789 display plus CHSC6x touch: short tap and long press aligned with the USER button for `ui-new`.
- **Release builds:** `build.sh build-firmware` for any `companion_radio_usb_tcp*` env now runs `mergebin` and copies `*-merged.bin` into `out/` next to the app-only bin (V3, V4 OLED, V4 TFT+touch).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device | Merged (recommended) | Non-merged |
|--------|----------------------|------------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.35/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.35/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin](prebuilt/releases/companion/v1.14.0.35/heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin) | [heltec_v4_tft_companion_radio_usb_tcp_touch.bin](prebuilt/releases/companion/v1.14.0.35/heltec_v4_tft_companion_radio_usb_tcp_touch.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.35/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.35/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.34 — 2026-03-22

**Firmware version:** v1.14.0.34 (meshcomod on upstream 1.14+).

**Highlights:**
- **meshcore-cli over TCP:** Fixes duplicate message lines ([issue #3](https://github.com/ALLFATHER-BV/meshcomod/issues/3)) by advancing sync history after a successful broadcast push of chat/channel frames, so `CMD_SYNC_NEXT_MESSAGE` does not re-deliver what you already received on the live push. Skips this when companion broadcast-to-all is disabled so other clients do not miss messages on sync.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.34/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.34/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.34/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.34/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.33 — 2026-03-21

**Firmware version:** v1.14.0.33 (meshcomod on upstream 1.14+).

**Highlights:**
- **Companion ↔ MeshCore app 1.14:** `CMD_SET_PATH_HASH_MODE` (61), device-info `path_hash_mode`, flood path-hash size from prefs; auto-add get/set includes `autoadd_max_hops`.
- **`/new_prefs` = upstream layout** (93 bytes) with **`rx_boosted_gain`**; automatic migration from legacy 91-byte Meshcomod prefs on first load.
- **SX1262/8:** Persisted RX boosted gain; applied at boot and after radio param changes.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.33/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.33/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.33/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.33/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.32 — 2026-03-14

**Firmware version:** v1.14.0.32 (meshcomod on upstream 1.14+).

**Highlights:**
- **Unread messages: long-press to dismiss all.** When viewing unread messages (clicking boot to go through them), long-press the boot button to clear all unread and return to the home screen so you don’t have to click through each one.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.32/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.32/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.32/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.32/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.31 — 2026-03-14

**Firmware version:** v1.14.0.31 (meshcomod on upstream 1.14+).

**Highlights:**
- **WiFi/NVS logs no longer in public chat:** Only read `wifi_ssid` / `wifi_pwd` from Preferences when the key exists (`isKey()` first). Avoids Arduino Preferences `getString()` logging "nvs_get_str len fail: wifi_ssid NOT_FOUND" to Serial, which was being forwarded into the mesh and shown in Channel 0.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.31/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.31/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.31/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.31/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.30 — 2026-03-14

**Firmware version:** v1.14.0.30 (meshcomod on upstream 1.14+).

**Highlights:**
- **V3 boot fix:** Prevents crash/reboot when device has no WiFi credentials (first boot or NVS erase). WiFi/tcpip stack is always started so the TCP server can bind without triggering LwIP "Invalid mbox" assert.
- **NVS:** Creates the `meshcomod` Preferences namespace on first boot when missing, eliminating repeated `nvs_open failed: NOT_FOUND` errors.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.30/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.30/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.30/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.30/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.29 — 2026-03-09

**Firmware version:** v1.14.0.29 (meshcomod on upstream 1.14+).

**Highlights:**
- **Resources tab:** % in use (not free). Sizes ≥ 1000 KB as 3.3K to fit the screen (e.g. RAM 78% 256/328, Flash 61% 3.9K/6.4K).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.29/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.29/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.29/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.29/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.28 — 2026-03-09

**Firmware version:** v1.14.0.28 (meshcomod on upstream 1.14+).

**Highlights:**
- **Device resources tab:** New tab on the home screen (V4/V3) showing CPU MHz, RAM free %, PSRAM free % (V4; n/a on V3), and Flash free %. One more dot before Shutdown.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.28/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.28/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.28/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.28/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.27 — 2026-03-09

**Firmware version:** v1.14.0.27 (meshcomod on upstream 1.14+).

**Highlights:**
- Same firmware as v1.14.0.26. Versioned release for procedure; pair with web client that has WebSocket ordered-delivery fix (contacts/chat over WiFi).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.27/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.27/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.27/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.27/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.26 — 2026-03-09

**Firmware version:** v1.14.0.26 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS/WS toggle:** WebSocket server can run as plain WS or WSS (TLS). Long-press on the WSS tab to switch; same port, choose `ws://` or `wss://` at runtime.
- **Default:** Boot with WSS off (plain WS). Use long-press on WSS tab to enable WSS when needed.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.26/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.26/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.26/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.26/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.25 — 2026-03-09

**Firmware version:** v1.14.0.25 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** delay(0) in handshake loop (yield only) so handshake completes before browser closes; 40 steps per poll.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.25/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.25/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.25/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.25/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.24 — 2026-03-09

**Firmware version:** v1.14.0.24 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** delay(1) on WANT_READ/WANT_WRITE in handshake loop so WiFi stack can deliver data (avoids BEACON_TIMEOUT / RESET).
- **WSS:** 30s handshake timeout per client for stuck connections.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.24/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.24/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.24/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.24/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.23 — 2026-03-09

**Firmware version:** v1.14.0.23 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** recv_timeout = NULL (no block, fixes RESET); 40 handshake steps per poll; tickWssHandshake() each loop so handshake advances twice per loop (reduces CLOSED).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.23/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.23/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.23/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.23/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.22 — 2026-03-09

**Firmware version:** v1.14.0.22 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** 15ms recv_timeout + 8 handshake steps per poll so TLS handshake receives client flights without long block; avoids ERR_CONNECTION_CLOSED while avoiding RESET.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.22/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.22/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.22/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.22/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.21 — 2026-03-09

**Firmware version:** v1.14.0.21 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** recv_timeout = NULL so TLS handshake never blocks; fixes ERR_CONNECTION_RESET for https://device:8765 and wss://.
- **WSS:** TLS 1.2 minimum, extended payload length fix, EOF handling retained.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.21/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.21/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.21/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.21/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.20 — 2026-03-09

**Firmware version:** v1.14.0.20 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** 100ms read timeout + recv_timeout callback so TLS handshake receives client data; fixes ERR_CONNECTION_CLOSED for https://device:8765 and wss://.
- **WSS:** TLS 1.2 minimum, extended payload length fix (TLS path), EOF handling in doHandshake and pollRecvFrame.
- **Heltec V3:** Declare `set_boot_phase` in variant target.h so V3 companion build succeeds.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.20/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.20/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.20/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.20/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.19 — 2026-02-24

**Firmware version:** v1.14.0.19 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS non-blocking recv/send:** Custom TLS bio returns WANT_READ/WANT_WRITE on EAGAIN; fixes ERR_CONNECTION_RESET for https://device:8765 and wss://.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.19/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.19/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.19/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.19/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.18 — 2026-02-24

**Firmware version:** v1.14.0.18 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS handshake 100ms read timeout:** mbedtls_ssl_conf_read_timeout(100) + recv_timeout so handshake receives data; fixes ERR_CONNECTION_CLOSED for https://device:8765 and wss://.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.18/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.18/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.18/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.18/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.17 — 2026-02-24

**Firmware version:** v1.14.0.17 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS handshake buffer 1536 bytes:** Fixes "unexpectedly closed" for https://device:8765 and wss:// (no truncation).
- **WSS/WS tab always on:** Tab after WiFi for all companion builds; label WSS or WS by build.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.17/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.17/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.17/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.17/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.16 — 2026-02-24

**Firmware version:** v1.14.0.16 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS handshake non-blocking:** mbedtls_ssl_set_bio recv_timeout = NULL so TLS handshake never blocks; fixes ERR_CONNECTION_RESET when opening https://deviceIP:8765 (cert page loads).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.16/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.16/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.16/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.16/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.15 — 2026-02-24

**Firmware version:** v1.14.0.15 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS cert page:** Opening https://deviceIP:8765 in a browser now returns 200 OK + HTML so you can accept the self-signed cert, then use wss:// from the client.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.15/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.15/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.15/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.15/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.14 — 2026-02-24

**Firmware version:** v1.14.0.14 (meshcomod on upstream 1.14+).

**Highlights:**
- **UI first in loop():** Run `ui_task.loop()` at start of every `loop()` so version screen dismisses at 3s even if mesh/serial blocks; fixes stuck version screen.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.14/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.14/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.14/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.14/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.13 — 2026-02-24

**Firmware version:** v1.14.0.13 (meshcomod on upstream 1.14+).

**Highlights:**
- **TCP/WSS defer 5s:** TCP and WSS server start deferred 5s after boot so version screen dismisses at 3s and USB works; fixes stuck version screen and GetContacts/GetChannel timeouts. WSS still at 10s + WiFi.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.13/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.13/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.13/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.13/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.12 — 2026-02-24

**Firmware version:** v1.14.0.12 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS boot fix:** mbedTLS init moved from global constructor to `begin()` (10s after boot, WiFi up). Fixes device stuck on "Loading..."; splash and home screen now appear.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.12/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.12/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.12/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.12/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.11 — 2026-02-24

**Firmware version:** v1.14.0.11 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS status tab:** New tab after WiFi/TCP shows WSS running or not, port, client count; when not running, explains 10s-after-WiFi start.
- WSS (TLS) on port 8765 restored; non-blocking. Build script and v1.14.0.10 unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.11/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.11/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.11/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.11/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.10 — 2026-02-24

**Firmware version:** v1.14.0.10 (meshcomod on upstream 1.14+).

**Highlights:**
- **Build script improvements:** `build-firmware <target>` no longer wipes `out/`; only bulk builds clear it so V4 then V3 bins accumulate. Platform detection for ESP32 mergebin/copy fixed (pio pipe + ESP32 fallback when bootloader/partitions present).
- Same firmware as v1.14.0.9 (plain ws:// port 8765; GetContacts over WiFi fix). WSS removed.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.10/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.10/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.10/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.10/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.9 — 2026-02-24

**Firmware version:** v1.14.0.9 (meshcomod on upstream 1.14+).

**Highlights:**
- **GetContacts over WiFi/WebSocket fixed:** WebSocket server-to-client frames with payload length ≥126 bytes now use RFC 6455 extended length encoding (2-byte **big-endian** length). The previous little-endian encoding caused browsers to drop or garble CONTACT (148-byte) and other large frames; START/END (5-byte) and small channel frames (<126) were unaffected.
- v1.14.0.8 behavior unchanged (optional WS frame debug, binary-only stream, reply-target pinning).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.9/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.9/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.9/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.9/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.8 — 2026-02-24

**Firmware version:** v1.14.0.8 (meshcomod on upstream 1.14+).

**Highlights:**
- **Optional WS frame debug:** Build with `-DWS_FRAME_DEBUG=1` to log each WebSocket send (client id, frame code 2=START/3=CONTACT/4=END, len, written). Use WiFi-only when enabled; see `src/helpers/esp32/DEBUG_WS_FRAME.md` for diagnosing GetContacts over WiFi.
- v1.14.0.7 behavior unchanged (binary-only companion stream, reply-target pinning).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.8/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.8/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.8/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.8/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.7 — 2026-03-06

**Firmware version:** v1.14.0.7 (meshcomod on upstream 1.14+). **Build date:** 06 Mar 2026.

**Highlights:**
- **Companion stream binary-only:** Removed contact-list Serial.printf from companion path. USB/WS/TCP now carry only framed protocol bytes; no ASCII debug in same stream, so parser is not contaminated. GetContacts and Web Serial should load contacts reliably.
- Retry and reply-target fix (v1.14.0.6) unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.7/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.7/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.7/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.7/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.6 — 2026-03-06

**Firmware version:** v1.14.0.6 (meshcomod on upstream 1.14+). **Build date:** 06 Mar 2026.

**Highlights:**
- **GetContacts over WiFi/WebSocket fixed:** Contact list completes: START (2) → N×CONTACT (3) → END (4). Reply target is saved when sending START and restored before each CONTACT/END so all frames go to the same client (fixes bug where CONTACT/END were sent to USB when USB was polled first).
- getReplyTarget/setReplyTarget in serial interface; contact list pins target for full sequence.
- Diagnostic logging and retries from v1.14.0.5 unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.6/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.6/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.6/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.6/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.5 — 2026-03-06

**Firmware version:** v1.14.0.5 (meshcomod on upstream 1.14+). **Build date:** 06 Mar 2026.

**Highlights:**
- **Contact list diagnostic logging (temporary):** Serial output when sending contact list: `contacts: sent START count=... ret=...`, `contacts: sent CONTACT i=... ret=...`, `contacts: sent END ret=...`. Use USB serial at 115200 to verify firmware is sending CONTACT/END.
- **Version and date:** Boot/device info show v1.14.0.5 and 06 Mar 2026; MyMesh.h fallback kept in sync.
- All v1.14.0.4 behavior unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.5/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.5/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.5/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.5/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.4 — 2026-02-24

**Firmware version:** v1.14.0.4 (meshcomod on upstream 1.14+).

**Highlights:**
- **WiFi WebSocket companion parity:** Contact list over `ws://<device-ip>:8765` now completes reliably. Retries for CONTACT/END frame writes so transient buffer full no longer drops the list; WebSocket/TCP no longer disconnect on first write failure so companion layer can retry. Same protocol as serial/TCP (binary frames, START → N×CONTACT → END).
- All v1.14.0.3 behavior (WebSocket server, Sync-Since) unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.4/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.4/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.4/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.4/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.3 — 2026-02-24

**Firmware version:** v1.14.0.3 (meshcomod on upstream 1.14+).

**Highlights:**
- **WebSocket server for browser-only WiFi:** When TCP is on and WiFi is up, device also listens on port **8765**. Connect with `ws://<device-ip>:8765` for device + browser only (no bridge). Status shows `ws: 8765` or `ws: off`.
- Sync-Since (62/61) and v1.14.0.2 behavior unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.3/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.3/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.3/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.3/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.2 — 2026-02-25

**Firmware version:** v1.14.0.2 (meshcomod on upstream 1.14+).

**Highlights:**
- **Sync-Since (for future custom client):** Command **62** (SyncSince) + response **61** (SyncSinceDone) for backfill after reconnect. Custom client must send 62 (not 60) and handle 61; stock clients unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.2/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.2/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.2/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.2/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.1 — 2026-02-24

**Firmware version:** v1.14.0.1 (meshcomod on upstream 1.14+).

**Highlights:**
- Boot splash: MeshCore logo removed; "meshcomod" text shown again as main title.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.1/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.1/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.1/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.1/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.0 — 2026-03-06

**Firmware version:** v1.14.0.0 (meshcomod on upstream 1.14+).

**Highlights:**
- Integrate official MeshCore upstream 1.14+ with all Meshcomod companion customizations on top.
- Preserve multi-transport (USB + TCP + BLE), per-client history/sync, BLE prioritization, WiFi runtime/reconnect, V3 stock-parity display, Meshcomod local command.
- Upstream 1.14 protocol: path_hash_mode, autoadd_max_hops in NodePrefs/DataStore; getAutoAddMaxHops() in MyMesh.
- Repeater and room-server builds verified.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.0/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.0/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/companion/v1.14.0.0/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/companion/v1.14.0.0/Heltec_v3_companion_radio_usb_tcp.bin) |

---

*For each new release, follow the [Release process](#release-process-do-this-for-every-new-fixrelease) above. Do not overwrite existing version folders.*
