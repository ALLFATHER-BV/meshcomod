## repeater-1.0.5 — 2026-03-22

**TCP repeater prebuilt** (Heltec WiFi LoRa 32 **V4** + **V3**). Train: **`repeater-X.Y.Z`**, independent of companion **`v1.14.0.x`**.

**Compile-time version string:** `meshcomod-repeater-1.0.5-73373c55` (git SHA at link).

**Changes vs repeater-1.0.4**

- **Branding:** Compile-time **`FIRMWARE_VERSION`** is prefixed with **`meshcomod-`** on **`build.sh`** **`*_repeater_tcp`** builds (OLED, serial, **`out/`** filenames).
- **OLED boot splash:** First **~3 s** match companion **ui-new** style — **`meshcomod`** title, version (trailing **`-<gitsha>`** hidden when it looks like a short hex SHA), build date, **Repeater** label (replaces MeshCore logo + “Please wait” during boot).
- **Device query (cmd 22):** **`REPEATER_COMPANION_FIRMWARE_VER_CODE` 27** — firmware string **40 bytes** at offset **60**, total payload **102** bytes (was 20 / 82). **meshcomod-client** (and any client using **`parseDeviceInfoBinaryV10`**) should be updated to handle **`payload.length >= 102`**.
- Includes **1.0.4** behaviour: MESHCM **OTA URL** line-ending trim (**HTTP 400** fix), GitHub raw rewrite, redirect follow, **`OTA:`** MESHCM lines.

**Build:** **`DISABLE_DEBUG=1`**.

**Images (app-only — flash at app offset, typically 0x10000):**

| Device | Binary |
|--------|--------|
| Heltec V4 | [heltec_v4_repeater_tcp.bin](heltec_v4_repeater_tcp.bin) |
| Heltec V3 | [Heltec_v3_repeater_tcp.bin](Heltec_v3_repeater_tcp.bin) |

**Procedure:** [`docs/REPEATER_RELEASE_PROCEDURE.md`](../../../docs/REPEATER_RELEASE_PROCEDURE.md). **Protocol / client:** [`docs/REPEATER_TCP_COMPANION.md`](../../../docs/REPEATER_TCP_COMPANION.md).
