# MeshCore 1.16.0 → fork integration: full change list + actions

Upstream `companion/repeater/room-server v1.16.0` (2026-06-06) vs the fork's base
(~`v1.15.0`, merge-base `ecd0cfc1`): **271 commits, 300 files**. Most are
`variants/*`/docs (clean); the work is ~12 core files the fork has diverged.

**Key fact:** the touch firmware compiles the *same* `src/` core + `examples/companion_radio/MyMesh` + CLI, so every protocol/core change below applies to it automatically. The *only* extra touch work is where `ui-touch/UITask.cpp` (or the fork's MyMesh) **calls an API that 1.16 changed** — flagged with **[TOUCH]**.

**Fork is already ahead:** `FIRMWARE_VER_CODE = 27` (upstream went 11→13), with its own CMDs/RESPs. So this is a *reconcile*, keeping the fork's protocol superset + grafting 1.16's additions — not a fast-forward.

---

## A. Protocol / wire format — INTEROP-CRITICAL (all firmware incl. touch)

| # | 1.16 change | Files | Fork action |
|---|---|---|---|
| A1 | **Extended ACK 4→6 bytes** (4 hash + attempt byte + random). `createAck`/`createMultiAck`/`sendAckTo` now take `(const uint8_t*, len)`; old `uint32_t` forms kept as inline wrappers. `onPeerDataRecv` builds + sends a 6-byte ACK. | `src/Mesh.cpp/.h`, `src/helpers/BaseChatMesh.cpp/.h` | Merge core as-is. **[TOUCH]** the fork's `expected_ack_table` (4-byte `uint32`) + `uiRegisterExpectedAck`/`onMessageAcked` + range-test `ack_hash` (`UITask.cpp:6230`) are 4-byte → extend to 6-byte, or keep a 4-byte-prefix match for basic interop. This drives the ✓/✓✓ delivery state, so verify it still fires. |
| A2 | **TRACE path offset `uint8`→`uint16`** (overflow fix when path_len×entry_size>255). | `src/Mesh.cpp` | Merge (bug fix). |
| A3 | **Anonymous / non-contact requests.** `MAX_ANON_CONTACTS=8`; `allocateContactSlot(transient_only)`; `addContact()` routes `ADV_TYPE_NONE` to transient slots; `CMD_SEND_ANON_REQ` allows unknown pubkeys. | `BaseChatMesh.*`, `MyMesh.cpp` | Merge. `contacts[]` grows by 8. **saveContacts now filters out `ADV_TYPE_NONE`** (`save_filter`) — reconcile with the fork's `saveContacts()`/`uiPersistContacts()`. `checkSerialInterface` skips anon contacts when exporting. |
| A4 | **Un-scoped flood.** `send_unscoped` bool; `CMD_SET_FLOOD_SCOPE_KEY` variant `1` sets it; `sendFloodScoped` bypasses scope when set. | `MyMesh.cpp/.h` | Add `send_unscoped` + the variant handler. |
| A5 | **`CMD_SEND_RAW_PACKET` (65)** — raw packet compose/send; needs `Dispatcher::tryParsePacket` (now public). | `MyMesh.cpp`, `src/Dispatcher.h` | Add the CMD handler + make `tryParsePacket` public. |
| A6 | **`CMD_DEVICE_QEURY` → `CMD_DEVICE_QUERY`** (typo rename); `RESP_CODE_DEVICE_INFO` comment. | `MyMesh.cpp` | Fork uses `CMD_DEVICE_QEURY` (companion `MyMesh.cpp:2441`, room-server alias). Rename (or keep a compat `#define`). |
| A7 | Companion proto **VER 11→13** + "open data types" doc. | `MyMesh.h`, docs | Fork is at **27** — graft the new CMDs (A4/A5) into the ver-27 protocol; web/phone client must learn them. |

## B. Radio / core (inherited by all firmware)

| # | 1.16 change | Files | Fork action |
|---|---|---|---|
| B1 | **`radio_set_params` / `radio_set_tx_power` / `radio_get_rng_seed` REMOVED** → `radio_driver.setParams` / `.setTxPower` / `.getRngSeed`. | radiolib wrappers; callers | **Update fork call sites:** `examples/kiss_modem/main.cpp` (3), `examples/simple_repeater/{MyMesh,main}.cpp` (6). Companion `MyMesh` already uses `radio_driver` in the fork — verify. |
| B2 | **Longer preamble for low SF** (`preambleLengthForSF`: SF≤8→32, else 16; `updatePreamble`). Range win. | `radiolib/RadioLibWrappers.*` + per-chip wrappers | Merge wrappers. |
| B3 | `radio.rxgain` CLI for LR1110; LR1110/SX1276/LLCC68/STM32WLx wrapper updates; KISS radio on most variants. | radiolib, CommonCLI | Merge. |
| B4 | `SimpleMeshTables.h` simplified (−47). | `src/helpers/SimpleMeshTables.h` | Take 1.16, re-verify (fork uses it). |

## C. CLI (`CommonCLI.cpp` — all firmware)

| # | 1.16 change | Fork action |
|---|---|---|
| C1 | **`NodePrefs` += `flood_max_unscoped`, `flood_max_advert`** (persisted: explicit load/save read/write). | Add to the fork's `NodePrefs` (which already has `path_hash_mode` etc.) — **mind the persisted field order/offsets**. |
| C2 | CLI: `flood.max.unscoped`, `flood.max.advert` (get/set); **`region def`** shorthand; `powersaving_enabled`; **upper-bound enforcement** for `rxdelay`/`txdelay`/`direct.txdelay`. | Merge into the fork's CommonCLI (which added `gps.baud` + touch CLI) — union. |

## D. Boards / power / displays (inherited)

| # | 1.16 change | Files | Fork action |
|---|---|---|---|
| D1 | `MainBoard::onBootComplete()` + `getIRQGpio()` virtuals. | `MeshCore.h` | Merge (no-op defaults). |
| D2 | **ESP32 light-sleep power saving** (`sleep()` via `esp_light_sleep_start`, LoRa-packet guard, GPIO wakeup) + `setCurrentTime`. | `ESP32Board.h` | Reconcile with the fork's ESP32Board OTA/touch superset. |
| D3 | **WiFi non-blocking reconnect** (event handler + 10 s loop poll); `onBootComplete()` call; `hasPendingWork()`→`sleep` auto power-save; **companion auto-shutdown on battery + UI warning**. | `main.cpp`, `MyMesh.*` | Reconcile with the fork's **recovery-first + touch boot** in `main.cpp`. **[TOUCH]** surface the low-batt auto-shutdown warning in the UI (optional). |
| D4 | **EnvironmentSensorManager redesign** (+506/−352) + RAK12035 fix. | sensors | Take 1.16's; re-apply the fork's `gps.baud` (`gps_baud`) touches. |
| D5 | New **`ui-tiny` U8g2 UI** + `U8g2Display.h`; **ST7789 redundant reset removed**; **buzzer startup-mute pref**; OLED/E213/E290/GxEPD tweaks; website on splash. | `ui-new`/`ui-orig`/`ui-tiny`, `ui/*`, `buzzer.*` | **`ui-touch/` is independent (no conflict)** — take upstream UIs for the non-touch envs. **[TOUCH]** the ST7789-reset + buzzer-mute changes apply to the touch build too (it uses `ST7789LCDDisplay` + buzzer) — fold in. |

## E. New boards (optional)
T-Echo Card, T-Impulse Plus, Station G3, Heltec T1, Xiao S3 variants, T-ETH Elite, HeltecV4 Expansion Kit + fixes (LNA default-off, GPS pins, batteries, RAK). Merge the variants we choose to ship (mostly clean new dirs).

---

## Integration approach
1. Branch `int-1.16` off the integrate branch; merge upstream @ the 1.16.0 tags; capture conflicts.
2. Reconcile **A** (protocol/ACK) first — interop. Then **B/C** (radio/CLI), **D** (boards/power/UI), **E** (variants).
3. Apply the **[TOUCH]** items in `ui-touch` (ACK width, radio_driver, CMD rename, ST7789/buzzer, optional UI for new features).
4. Build the **whole matrix**: both touch envs + recovery + ~8 non-touch companion + repeater + room + kiss_modem.
5. **Hardware interop test** against a 1.16 node: message delivery + **ACK/✓✓** (A1), group msg, anon req.
6. Cut a tagged release (beta_3).

## Open decisions
- **Scope:** protocol-critical subset first (A1/A3/A4/A5/A7 — keep interop as the net moves to 1.16), then the rest? Or full merge in one pass.
- **ACK width [TOUCH]:** full 6-byte adoption (best interop) vs 4-byte-prefix match (less churn).
- **New boards:** all, or only ones we ship.

---

## ✅ RESOLVED — merged 2026-06-07 (branch `int-1.16`)

Done as a real `git merge` of upstream 1.16.0 (`07a3ca9e`) into the fork: conflicts
reconciled in merge commit **`6660a301`**, then post-merge build fixes in **`588debad`**.
Chosen **full merge in one pass** + **4-byte-prefix ACK match** (least churn, keeps interop).

### Conflict resolutions (merge commit `6660a301`)
- **A1 ACK 4→6 byte** — auto-merged in `Mesh`/`BaseChatMesh`; the fork's 4-byte
  `expected_ack` matches the 6-byte ACK's 4-byte hash prefix → node↔node interop kept
  with no MyMesh/touch change. *(✓✓ delivery state still wants a live interop check.)*
- **A3 anon/transient contacts** — kept the fork's PSRAM-lazy `contacts` pointer, now
  sized `MAX_CONTACTS+MAX_ANON_CONTACTS`; took 1.16 `allocateContactSlot(transient_only)`.
- **A4 un-scoped flood** — took 1.16's `send_unscoped`/`default_scope` in
  `sendFloodScoped`; the fork's `path_hash_mode` still applies (un-scoped branch passes
  it; scoped branch routes through the lower overload).
- **A5 raw packet** (CMD 65) + public `tryParsePacket` — present (auto-merged in).
- **A6 `CMD_DEVICE_QUERY`** — value unchanged (22) ⇒ wire-compatible; only the upstream
  typo symbol was corrected.
- **`CMD_SET_FLOOD_SCOPE`** — 1.16 renamed it `_KEY`; kept the **fork's** name/number
  (54) for client compat and adopted 1.16's `==1` `send_unscoped` sub-branch under it.
- **B1 radio API** — `radio_set_params/_tx_power/get_rng_seed` removed upstream →
  `radio_driver.setParams/.setTxPower/.getRngSeed` at all fork call sites.
- **C1 `NodePrefs`** — the companion's own struct is **untouched** ⇒ saved-prefs upgrade
  compat preserved; `flood_max_*` landed only in the repeater/room `CommonCLI.h` struct.
- **D3 WiFi** — `main.cpp`: union of 1.16 `setAutoReconnect`/`onEvent` + the fork's
  runtime-creds `begin`; loop keeps fork OTA poll + 1.16 idle-sleep + reconnect (the
  reconnect block gated `!MULTI_TRANSPORT_COMPANION`, which has its own SM).
- **Versioning** — `FIRMWARE_VER_CODE 27` kept; `FIRMWARE_VERSION v1.16.0-touch`.
- **platformio** — split the env git collapsed (fork `repeater_tcp` + 1.16
  `expansionkit_repeater`); added 1.16 `kiss_modem` (heltec_v4 / wireless_paper /
  T-Deck); T-Deck usb/ble standard envs → `ui-new`.
- **Branding kept** — `ui-new` meshcomod splash, `simple_repeater` (no meshcore.io),
  fork CI matrix, README.

### Post-merge build fixes (commit `588debad`)
- `kiss_modem/main.cpp`: define the fork's `set_boot_phase()` hook (board `target.cpp`
  + `CustomSX1262.h` call it; upstream's KISS main didn't) → fixes link on all kiss envs.
- `ui-new/UITask.cpp`: TCP-page closing `}` was inside `#ifdef MULTI_TRANSPORT_COMPANION`,
  dangling the next `else if` when undefined (exposed by moving T-Deck usb/ble to ui-new).
  Close the block unconditionally.
- radio API call sites + the WiFi `!MULTI_TRANSPORT` guard + the CMD-name fix above.

### Build matrix — all green ✅
- **touch:** `heltec_v4_tft_companion_radio_usb_tcp_touch`, `LilyGo_TDeck_companion_radio_touch`
- **companion:** `heltec_v4_companion_radio_usb_tcp`, `LilyGo_TDeck_companion_radio_{usb,ble}`
- **repeater/room:** `heltec_v4_repeater_tcp`, `heltec_v4_expansionkit_repeater`,
  `LilyGo_TDeck_repeater`, `Heltec_Wireless_Paper_room_server_multitransport`
- **kiss:** `heltec_v4_kiss_modem`, `LilyGo_TDeck_kiss_modem`, `Heltec_Wireless_Paper_kiss_modem`

### Still TODO
- **Hardware interop test** vs a real 1.16 node: message delivery + ✓✓ ACK (A1), group
  msg, anon req, raw packet.
- Teach the web/phone client the new CMDs (raw packet / un-scoped flood) if we want to
  surface them — currently firmware-side only.
- Cut a `beta_N` drop once interop is confirmed.
