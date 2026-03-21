# Meshcomod / custom client ↔ Wi‑Fi repeater (TCP & WebSocket)

This document is a **practical integration spec** so you can add a **repeater / Wi‑Fi settings screen** (and optional power‑user stats) in your custom meshcomod client. It matches the **`heltec_v4_repeater_tcp`** / **`Heltec_v3_repeater_tcp`** firmware in this repo.

**Repeater firmware version** (device query / UI) uses the **`repeater-X.Y.Z`** release train, independent of companion **`v1.14.0.x`** — see [`REPEATER_RELEASE_PROCEDURE.md`](REPEATER_RELEASE_PROCEDURE.md).

---

## 1. Can you do it over TCP *and* WebSocket?

**Yes.** The repeater speaks one **binary companion protocol** on:

- **TCP** port `5000` (default)
- **WebSocket** port `8765` (default; `ws://` unless you build with `WS_USE_TLS=1`)

Framing is identical: frame prefix **`<`** (0x3C), **2-byte little-endian** payload length, then payload; responses use **`>`** (0x3E) with the same length header. Your existing `NodeConnection` + `buildOutgoingFrame()` path works unchanged—only the transport (TCP vs WS) differs.

**Prerequisite:** The device must have **Wi‑Fi associated** and **TCP+WS companion transport** enabled on the device (default on; see limitations below).

---

## 2. How to know the connected device is this repeater

Use **`CMD_DEVICE_QUERY` (22)** or **`CMD_APP_START` (1)** after connect:

- **Device query** response (**`RESP_DEVICE_INFO` = 13**): repeater reports **0 contacts**, **0 channels**, no BLE features—use as a heuristic together with **SelfInfo**.
- **App start** response (**`RESP_SELF_INFO` = 5**): advert / node type should indicate **repeater** (meshcomod already parses SelfInfo for name, radio, etc.).

If you only want to show the **Wi‑Fi repeater** screen when appropriate, gate on **SelfInfo repeater type** + **zero channels** (or a explicit firmware version string from device info when you add one).

Constants and command list: [`examples/repeater_tcp/repeater_companion_proto.h`](../examples/repeater_tcp/repeater_companion_proto.h).

---

## 3. Two channels of control: binary commands vs “mesh text”

| Mechanism | Use for | Works on repeater? |
|-----------|---------|-------------------|
| **Binary frames** (`nodeConnection.send(buildOutgoingFrame(payload))`) | Radio, name, time, battery, stats, reboot, etc. | **Yes** for the [supported subset](#5-supported-binary-commands-subset) |
| **`CMD_SEND_TXT_MSG` (2)** + **`TXT_TYPE_PLAIN` (0)** + **MESHCM** | **Wi‑Fi helper** chat (`wifi status`, `wifi on`, …) | **Yes** — `RESP_SENT` + `PUSH_SEND_CONFIRMED` + **code 16** text chunks |
| **`CMD_SEND_TXT_MSG` (2)** + **`TXT_TYPE_CLI_DATA` (1)** + **MESHCM** | **Full MeshCore CLI** (same as USB serial), incl. **`get` / `set advert.interval`**, **`get` / `set flood.advert.interval`** | **Yes** — response in **`PUSH_BINARY_RESPONSE` (0x8C)** only (see [§3.3](#33-cli_data-meshcm--binary-response-0x8c)) |

### 3.1 MESHCM destination (SendTxtMsg)

Repeater only accepts **SendTxtMsg** when the **6-byte pubkey prefix** is **ASCII `MESHCM`**:

`0x4D, 0x45, 0x53, 0x48, 0x43, 0x4D`

Use the same **`buildSendTxtMsgPayload`** (or equivalent): **txtType** (`0` = plain, `1` = CLI), **attempt**, **senderTimestamp**, **6-byte prefix**, **UTF‑8 text** body.

**Plain (`TXT_TYPE_PLAIN`):** device sends **`RESP_SENT` (6)** (ack = your timestamp), **`PUSH_SEND_CONFIRMED` (0x82)**, then **`RESP_CONTACT_MSG_RECV_V3` (16)** multi-part text + **`PUSH_MSG_WAITING` (0x83)** — good for human chat and **wifi …** lines.

**CLI (`TXT_TYPE_CLI_DATA`):** device sends **`RESP_SENT`** only (no 0x82), then **one** **`PUSH_CODE_BINARY_RESPONSE` (0x8C)** + **`0x83`** tickle. **Do not** expect code 16 for CLI lines.

### 3.2 Wi‑Fi CLI commands (plain text only)

Implemented in firmware (`handleRepeaterMeshcomodLine`) when **txtType is PLAIN**:

| Command | Purpose |
|---------|---------|
| `help` | Lists commands |
| `status` | Short repeater / Wi‑Fi summary |
| `wifi status` | **`radio_enabled=0|1`**, `ssid=…`, `connected=0|1`, optional `ip=…` |
| `wifi on` / `wifi off` | NVS + immediate **STA power** (off drops LAN until USB or `wifi on`) |
| `wifi scan` | Scan results (requires radio on) |
| `wifi use <n>` | Pick SSID from last scan |
| `wifi set ssid "…"` / `wifi set pwd "…"` | NVS credentials |
| `wifi apply` | Reconnect (fails if `radio_enabled=0`) |
| `wifi clear` | Clear stored SSID/password |

**Parsing tip:** `wifi status` lines include **`radio_enabled=0`** or **`radio_enabled=1`** — regex: `\bradio_enabled=([01])\b`.

**USB serial:** Same Wi‑Fi helpers exist as `set wifi.*` / `get wifi.*` lines on the repeater’s USB CLI (see [`REPEATER_TCP_COMPANION.md`](REPEATER_TCP_COMPANION.md)).

### 3.3 `TXT_TYPE_CLI_DATA` + MESHCM → binary response (0x8C)

For **auto-advert settings** (and any other **CommonCLI** command), send **`CMD_SEND_TXT_MSG`** with **`TXT_TYPE_CLI_DATA = 1`**.

**Wire payload (after command byte 2):**

`[0x01][attempt][ts0][ts1][ts2][ts3]['M']['E','S','H','C','M'][UTF-8 command line…]`

**Device → client (order):**

1. **`RESP_SENT` (6):** byte 0 = 6, byte 1 = 0, bytes 2–5 = **ack tag** (same as your **senderTimestamp** if non-zero, else device-generated).
2. **`PUSH_CODE_BINARY_RESPONSE` (0x8C):** `[0x8C][0x00][tag 4 LE][UTF-8 body…]`  
   - **tag** matches bytes 2–5 of **`RESP_SENT`** — use this to pair request/response in the UI.  
   - **body** = CLI output. For **`get advert.interval`** / **`get flood.advert.interval`**, firmware normalizes a lone **`> <integer>`** to **just the integer** (e.g. `12`) so parsers can use `parseInt` on the whole payload. Other replies (e.g. `OK`, errors) pass through unchanged.
3. **`PUSH_MSG_WAITING` (0x83)** — single byte, same as companion “poll for messages” tickle.

**CLI commands (persist in `NodePrefs` / flash via existing CommonCLI):**

| Command | Meaning |
|---------|---------|
| `get advert.interval` | **Local / zero-hop** auto-advert period in **minutes** (`0` = off). Stored internally as half-minutes; device reports **actual minutes** (even integer). |
| `get flood.advert.interval` | **Flood** auto-advert period in **hours** (`0` = off). |
| `set advert.interval <minutes>` | Valid when **off** (`0`) or **60–240** minutes (MeshCore `MIN_LOCAL_ADVERT_INTERVAL` = 60). |
| `set flood.advert.interval <hours>` | Valid when **off** (`0`) or **3–168** hours (non-zero). |

**Ordering:** Send one CLI line per request and match **one** 0x8C to each **`RESP_SENT`** via tag; avoid interleaving other CLI traffic on the same connection if your UI assumes strict order.

---

## 4. Suggested “Wi‑Fi repeater settings” screen (client UX)

Split into blocks so casual vs power users stay sane:

1. **Connection / identity** — From SelfInfo + DeviceInfo: name, firmware, battery (existing `CMD_GET_BATT_AND_STORAGE` / KPI poll).
2. **Wi‑Fi** — Buttons: **Refresh** (`wifi status`), **Radio on**, **Radio off**; optional fields for SSID/password + **Apply** implemented as sending the right **wifi set** / **wifi apply** lines via **SendTxtMsg → MESHCM** (or guide user to mesh chat). Show last parsed `radio_enabled` + `connected` + `ip` from replies.
3. **Companion transport** — **Read-only note:** Turning **TCP+WS** off is currently **OLED long‑press on Network page** (`repeater_transport_enabled`). There is **no** binary command for this on the repeater image today; document in UI (“Use device button”) or add a firmware command later if you need remote control.
4. **Radio / node** — Reuse existing Settings payloads: **`CMD_SET_ADVERT_NAME` (8)**, **`CMD_SET_RADIO_PARAMS` (11)**, **`CMD_SET_RADIO_TX_POWER` (12)**, **`CMD_SET_ADVERT_LATLON` (14)**, tuning / other / path hash as needed (all supported on repeater per table below).
5. **Auto adverts** — Use **`SendTxtMsg` + `TXT_TYPE_CLI_DATA`** + MESHCM: `get advert.interval` / `get flood.advert.interval`; parse **0x8C** body as integer minutes/hours. **Set** with `set advert.interval <n>` / `set flood.advert.interval <n>` (same transport).
6. **Power user / stats** — Poll **`CMD_GET_STATS` (56)** (see [§6](#6-cmd_get_stats-56--layouts)) on a timer when this screen or Advanced tab is visible.

---

## 5. Supported binary commands (subset)

Full table: [`REPEATER_TCP_COMPANION.md`](REPEATER_TCP_COMPANION.md) § Supported commands.

Summary for the client:

| Cmd (dec) | Name | Notes |
|-----------|------|--------|
| 22 | Device query | Model, version, storage, … |
| 1 | App start | SelfInfo |
| 2 | Send text | **MESHCM** only: **PLAIN** → Wi‑Fi chat; **CLI_DATA** → full serial CLI + **0x8C** reply |
| 4 | Get contacts | Always **0** contacts |
| 10 / 62 | Sync | No message history |
| 5 / 6 | Get / set device time | |
| 8 | Set advert name | |
| 14 | Set advert lat/lon | |
| 7 | Send self advert | |
| 11 / 12 | Set radio params / TX power | |
| 21 / 43 | Set / get tuning params | |
| 38 | Set other params | Partial |
| 61 | Set path hash mode | |
| 19 | Reboot | Payload `reboot` after cmd byte |
| 20 | Battery + storage | |
| **56** | **Get stats** | **Subtype byte** 0 / 1 / 2 |
| 23 / 24 | Export / import private key | If enabled in build |

**Unsupported:** `CMD_FACTORY_RESET` (51) → error.

Payload layouts for most commands match **meshcomod** `companion_serial.ts` builders used for the full companion; if a builder is missing for the repeater-only path, copy lengths from **companion_radio** `MyMesh.cpp` command handlers.

---

## 6. `CMD_GET_STATS` (56) — layouts

Request: **`[56, subtype]`** where **subtype** is:

- `0` = **STATS_TYPE_CORE**
- `1` = **STATS_TYPE_RADIO**
- `2` = **STATS_TYPE_PACKETS**

Response: first byte **`RESP_CODE_STATS` = 24**, second byte **subtype**, then fields (**little-endian**):

### Subtype 0 — Core (11 bytes total payload)

| Offset | Size | Field |
|--------|------|--------|
| 0 | 1 | `0x18` (24) = RESP_CODE_STATS |
| 1 | 1 | `0` = CORE |
| 2 | 2 | Battery mV (`uint16`) |
| 4 | 4 | Uptime seconds (`uint32`) |
| 8 | 2 | Error flags (`uint16`) |
| 10 | 1 | TX queue length (`uint8`) |

### Subtype 1 — Radio (14 bytes total)

| Offset | Size | Field |
|--------|------|--------|
| 0 | 1 | 24 |
| 1 | 1 | `1` = RADIO |
| 2 | 2 | Noise floor (`int16`, dBm typical) |
| 4 | 1 | Last RSSI (`int8`) |
| 5 | 1 | Last SNR **×4** (`int8`, 0.25 dB steps) |
| 6 | 4 | TX airtime seconds (`uint32`) |
| 10 | 4 | RX airtime seconds (`uint32`) |

### Subtype 2 — Packets (34 bytes total)

| Offset | Size | Field |
|--------|------|--------|
| 0 | 1 | 24 |
| 1 | 1 | `2` = PACKETS |
| 2 | 4 | Packets recv (total) |
| 6 | 4 | Packets sent (total) |
| 10 | 4 | Sent flood |
| 14 | 4 | Sent direct |
| 18 | 4 | Recv flood |
| 22 | 4 | Recv direct |
| 26 | 4 | Recv errors |

**Client work:** add `CMD_GET_STATS = 56`, `RESP_STATS = 24`, `buildGetStatsPayload(subtype)`, `parseStatsPayload(...)`, then call from your repeater screen or Advanced dashboard. Wire **`processOneIncomingFrame`** (or equivalent) to resolve a Promise if you use request/response pairing.

Reference implementation: [`examples/simple_repeater/MyMesh.cpp`](../examples/simple_repeater/MyMesh.cpp) (`handleRepeaterTcpCompanionCommand`), mirroring [`examples/companion_radio/MyMesh.cpp`](../examples/companion_radio/MyMesh.cpp).

---

## 7. meshcomod repo pointers (where to implement)

Typical files under **`Meshcomod-client/`**:

| Area | File / area |
|------|-------------|
| Frame builders | `src/protocol/companion_serial.ts` — add stats + any missing repeater payloads |
| Web UI | `apps/web-client/src/main.ts` — new tab/panel, gated on “repeater” detection |
| Transport | `src/transports/tcp_transport.ts`, `websocket_transport.ts` — no protocol change |
| Session | `src/core/node_connection.ts` — unchanged if you use same `send()` |

You can **duplicate the pattern** already used for:

- **Wi‑Fi radio** buttons → `sendDeviceWifiMeshCommand` + channel/MESHCM SendTxtMsg (see existing Settings → Device section)
- **Device query / battery** → existing payloads + polling

---

## 8. Limitations & safety

- **Turning Wi‑Fi radio off** over **TCP/WS** will **drop the connection** you’re using—expect disconnect; recovery via **USB** or physical access.
- **TCP+WS companion off** (OLED) cannot be toggled from the client **without new firmware**.
- **No authentication** on raw TCP/WS—treat as **trusted LAN** only.
- **Plain WS** from **HTTPS** pages is often blocked by browsers; use **HTTP** to the client, **WSS** build, or a browser extension / native wrapper.

---

## 9. Checklist before shipping your screen

- [ ] Connect via **TCP** and **WS** with same code path (`buildOutgoingFrame`).
- [ ] Detect repeater (SelfInfo + Device query).
- [ ] **Wi‑Fi:** send **SendTxtMsg** with **MESHCM** prefix; parse **`radio_enabled`** from `wifi status`.
- [ ] **Stats (optional):** implement **56/24** request/response and poll every N seconds.
- [ ] Document **OLED-only** companion transport toggle in UI.
- [ ] Test **wifi off** recovery path (USB serial or second interface).

---

## See also

- [`REPEATER_TCP_COMPANION.md`](REPEATER_TCP_COMPANION.md) — OLED, ports, full command table  
- [`repeater_companion_proto.h`](../examples/repeater_tcp/repeater_companion_proto.h) — constants  
- [`WifiRuntimeStore`](../src/helpers/esp32/WifiRuntimeStore.cpp) — NVS keys (`meshcomod`, `wifi_ssid`, `wifi_pwd`, `wifi_radio_en`)
