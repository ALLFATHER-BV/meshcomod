# Web client ↔ Heltec V4 touch UI — functionality parity matrix

This document maps **Meshcomod web client** capabilities (companion protocol + UI) to the **on-device touch UI** (`examples/companion_radio/ui-new/UITask.*`) and the **serial/WebSocket command/response** codes implemented in firmware (`examples/companion_radio/MyMesh.cpp`).

**Rule:** Touch UI must use the same **companion framing** and **mesh send/receive paths** as the web client; it does not introduce alternate on-air semantics.

| Web client area | Web client surface (reference) | Touch UI surface (Heltec V4) | Companion / mesh mapping |
|-----------------|--------------------------------|------------------------------|----------------------------|
| **Connect / session** | USB / BLE / Wi‑Fi transports, session replay | **Home:** connection summary. **Net:** contacts + connection text. **Set → Transports:** TCP / BLE / Wi‑Fi (STA radio) toggles + WS hint + **Reset path** | Same node; `_serial->isConnected()`, transport flags via `BaseSerialInterface`; Wi‑Fi uses `MyMesh::meshcomodSetWifiRadio` → same path as meshcomod `wifi on` / `wifi off` |
| **DM chat** | Thread list + timeline + composer | Chat tab: thread list, timeline, composer | **Send:** `CMD_SEND_TXT_MSG` path in `MyMesh::handleCmdFrame` → `sendMessage()`; **Touch:** calls `the_mesh.sendMessage()` directly (equivalent payload path) |
| **Channel chat** | Channel threads + composer | Chan tab | **Send:** `CMD_SEND_CHANNEL_TXT_MSG` → `sendGroupMessage()`; **Touch:** `the_mesh.sendGroupMessage()` with `getChannel(slot)` |
| **Receive / sync** | `PUSH_MSG_WAITING`, `CMD_SYNC_NEXT_MESSAGE`, history | Timeline fed by `AbstractUITask::newMsg` / `notify` from `MyMesh` (`queueMessage`, `onChannelMessageRecv`) | Same push/history ring; touch does not reimplement sync client |
| **Optimistic / status** | Optimistic rows + ack UI | Outgoing line appended locally; alerts for send result / delivery | ACK path still pushes `PUSH_CODE_SEND_CONFIRMED` to serial clients; touch shows send result via alerts |
| **Contacts list** | Full CRUD, filters, favorites | **Net tab:** scrollable contact list → opens DM thread | **Data:** `getNumContacts` / `getContactByIdx`; **Commands:** `CMD_GET_CONTACTS`, `CMD_ADD_UPDATE_CONTACT`, `CMD_REMOVE_CONTACT`, etc. (from web/serial) |
| **Channels list** | Load/set slots, secrets | **Chan tab** + threads seeded from `getChannel(i)` | `CMD_GET_CHANNEL` / `CMD_SET_CHANNEL` |
| **Room server** | Login, ACL, posts | Not a dedicated touch flow | `CMD_SEND_LOGIN`, `CMD_LOGOUT`, room frames — parity **partial** until dedicated UI |
| **Settings / device** | Tabs: profile, radio, auto-add, transports, device, experimental (+ OTA/backup/map in web) | **Set tab:** WhatsApp-style **category list** (6 rows). Each opens a full-screen overlay: **Profile** (name, lat/lon, share position), **Radio** (freq/mod/TX/airtime + reboot prompt), **Auto-add**, **Transports**, **Device** (identity/stats, time/advert/reboot, GPS/buzzer, **diagnostics log**), **Experimental**. **Out of scope on touch:** backup import/export, OTA catalog, map/preset pickers — use web/serial. | Writes go through `NodePrefs` / `the_mesh.savePrefs()`, `applyRadioFromPrefs()`, `advert()`, RTC, `_board->reboot()` — same persistence as companion; no new packet/storage formats |
| **Repeater / transport** | Wi‑Fi / repeater tools | **Set → Transports** (toggles + reset path); provisioning flow unchanged | Path: `CMD_RESET_PATH` (serial); touch: `resetActiveDmPath` when contact resolved |
| **Diagnostics** | Rx log, CLI, dashboard | **Set → Device** footer: compact rolling log; **Home** stats unchanged | `CMD_GET_STATS`, trace/telemetry — touch shows **local** UI/diag lines; deep CLI remains serial/web |
| **Persistence** | IndexedDB + localStorage | `Preferences` namespace `meshTouch` (active tab); `NodePrefs` / `DataStore` for node prefs | Same store files as companion build |

## Command quick reference (firmware `MyMesh.cpp`)

| ID | Symbol | Role |
|----|--------|------|
| 1 | `CMD_APP_START` | Session start / self info |
| 2 | `CMD_SEND_TXT_MSG` | DM plain text |
| 3 | `CMD_SEND_CHANNEL_TXT_MSG` | Channel plain text |
| 4 | `CMD_GET_CONTACTS` | Contact dump / sync |
| 10 | `CMD_SYNC_NEXT_MESSAGE` | Pull next history message |
| 31 / 32 | `CMD_GET_CHANNEL` / `CMD_SET_CHANNEL` | Channel slots |
| 13 | `CMD_RESET_PATH` | Clear stored route |
| 20 | `CMD_GET_BATT_AND_STORAGE` | Battery / storage |
| 56 | `CMD_GET_STATS` | Stats subtypes (v8+) |

Push codes include `PUSH_CODE_MSG_WAITING` (1) and `PUSH_CODE_SEND_CONFIRMED` for ACK delivery to **connected** clients.

## Parity gaps (acceptable / backlog)

| Item | Status |
|------|--------|
| Full room-server UX | Backlog — protocol exists |
| MeshCore backup import/export on device | **Intentionally not in touch UI** — storage constrained; use web client |
| OTA catalog / map picker / preset search on device | **Intentionally not in touch UI** — use web client |
| Full Rx log + export | Partial — compact on-device log only |
| Trace / path / binary helpers from bubbles | Backlog — use web client or serial for deep tools |

## Related docs

- `docs/companion_protocol.md` — framing overview  
- `docs/REPEATER_MESHCOMOD_CLIENT_INTEGRATION.md` — client integration  
- `docs/HARDWARE_TOUCH_PARITY_CHECKLIST.md` — acceptance tests  
