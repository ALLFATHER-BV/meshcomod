# Release log

Versioned prebuilts are listed here so you can **roll back** if a newer release causes issues. The **latest** build is always in [`prebuilt/`](prebuilt/) (same files are updated on each release). For a specific version, use the links below.

## Release process (do this for every new fix/release)

**Rule: Every new fix or release gets a new version number. Never overwrite an existing version's binaries.**

1. **Bump version** — Choose the next version (e.g. `v1.14.0.1`). Do not reuse the current version.
2. **Build** — Build both Heltec V4 and V3 USB+TCP firmware: `export FIRMWARE_VERSION=v1.14.0.1` then `sh build.sh build-firmware heltec_v4_companion_radio_usb_tcp` and `sh build.sh build-firmware Heltec_v3_companion_radio_usb_tcp`.
3. **Create versioned folder** — Create `prebuilt/releases/<version>/` and copy the four prebuilt bins into it (and into `prebuilt/` as latest). E.g. run `sh scripts/copy-release-bins.sh v1.14.0.1`. Add `notes.md` in the version folder (often done in step 4).
4. **Update this file** — Add a new section above with the new version number, date, highlights, and table linking to the new release paths.
5. **Commit and push** — Always commit and push: stage version bump, prebuilts, `prebuilt/releases/<version>/` (bins + notes.md), and RELEASES.md; commit with a short message (e.g. `v1.14.0.N: <highlights>`); push to the release remote (e.g. `git push allfather main`). Do not skip this step.

**Summary:** `prebuilt/` = latest only. `prebuilt/releases/<version>/` = one folder per version (with bins + `notes.md`); keep all of them so users can roll back.

---

## v1.14.0.32 — 2026-03-14

**Firmware version:** v1.14.0.32 (meshcomod on upstream 1.14+).

**Highlights:**
- **Unread messages: long-press to dismiss all.** When viewing unread messages (clicking boot to go through them), long-press the boot button to clear all unread and return to the home screen so you don’t have to click through each one.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.32/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.32/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.32/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.32/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.31 — 2026-03-14

**Firmware version:** v1.14.0.31 (meshcomod on upstream 1.14+).

**Highlights:**
- **WiFi/NVS logs no longer in public chat:** Only read `wifi_ssid` / `wifi_pwd` from Preferences when the key exists (`isKey()` first). Avoids Arduino Preferences `getString()` logging "nvs_get_str len fail: wifi_ssid NOT_FOUND" to Serial, which was being forwarded into the mesh and shown in Channel 0.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.31/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.31/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.31/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.31/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.30 — 2026-03-14

**Firmware version:** v1.14.0.30 (meshcomod on upstream 1.14+).

**Highlights:**
- **V3 boot fix:** Prevents crash/reboot when device has no WiFi credentials (first boot or NVS erase). WiFi/tcpip stack is always started so the TCP server can bind without triggering LwIP "Invalid mbox" assert.
- **NVS:** Creates the `meshcomod` Preferences namespace on first boot when missing, eliminating repeated `nvs_open failed: NOT_FOUND` errors.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.30/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.30/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.30/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.30/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.29 — 2026-03-09

**Firmware version:** v1.14.0.29 (meshcomod on upstream 1.14+).

**Highlights:**
- **Resources tab:** % in use (not free). Sizes ≥ 1000 KB as 3.3K to fit the screen (e.g. RAM 78% 256/328, Flash 61% 3.9K/6.4K).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.29/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.29/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.29/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.29/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.28 — 2026-03-09

**Firmware version:** v1.14.0.28 (meshcomod on upstream 1.14+).

**Highlights:**
- **Device resources tab:** New tab on the home screen (V4/V3) showing CPU MHz, RAM free %, PSRAM free % (V4; n/a on V3), and Flash free %. One more dot before Shutdown.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.28/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.28/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.28/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.28/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.27 — 2026-03-09

**Firmware version:** v1.14.0.27 (meshcomod on upstream 1.14+).

**Highlights:**
- Same firmware as v1.14.0.26. Versioned release for procedure; pair with web client that has WebSocket ordered-delivery fix (contacts/chat over WiFi).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.27/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.27/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.27/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.27/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.26 — 2026-03-09

**Firmware version:** v1.14.0.26 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS/WS toggle:** WebSocket server can run as plain WS or WSS (TLS). Long-press on the WSS tab to switch; same port, choose `ws://` or `wss://` at runtime.
- **Default:** Boot with WSS off (plain WS). Use long-press on WSS tab to enable WSS when needed.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.26/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.26/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.26/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.26/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.25 — 2026-03-09

**Firmware version:** v1.14.0.25 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** delay(0) in handshake loop (yield only) so handshake completes before browser closes; 40 steps per poll.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.25/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.25/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.25/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.25/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.24 — 2026-03-09

**Firmware version:** v1.14.0.24 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** delay(1) on WANT_READ/WANT_WRITE in handshake loop so WiFi stack can deliver data (avoids BEACON_TIMEOUT / RESET).
- **WSS:** 30s handshake timeout per client for stuck connections.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.24/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.24/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.24/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.24/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.23 — 2026-03-09

**Firmware version:** v1.14.0.23 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** recv_timeout = NULL (no block, fixes RESET); 40 handshake steps per poll; tickWssHandshake() each loop so handshake advances twice per loop (reduces CLOSED).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.23/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.23/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.23/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.23/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.22 — 2026-03-09

**Firmware version:** v1.14.0.22 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** 15ms recv_timeout + 8 handshake steps per poll so TLS handshake receives client flights without long block; avoids ERR_CONNECTION_CLOSED while avoiding RESET.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.22/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.22/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.22/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.22/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.21 — 2026-03-09

**Firmware version:** v1.14.0.21 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** recv_timeout = NULL so TLS handshake never blocks; fixes ERR_CONNECTION_RESET for https://device:8765 and wss://.
- **WSS:** TLS 1.2 minimum, extended payload length fix, EOF handling retained.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.21/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.21/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.21/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.21/Heltec_v3_companion_radio_usb_tcp.bin) |

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
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.20/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.20/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.20/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.20/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.19 — 2026-02-24

**Firmware version:** v1.14.0.19 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS non-blocking recv/send:** Custom TLS bio returns WANT_READ/WANT_WRITE on EAGAIN; fixes ERR_CONNECTION_RESET for https://device:8765 and wss://.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.19/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.19/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.19/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.19/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.18 — 2026-02-24

**Firmware version:** v1.14.0.18 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS handshake 100ms read timeout:** mbedtls_ssl_conf_read_timeout(100) + recv_timeout so handshake receives data; fixes ERR_CONNECTION_CLOSED for https://device:8765 and wss://.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.18/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.18/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.18/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.18/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.17 — 2026-02-24

**Firmware version:** v1.14.0.17 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS handshake buffer 1536 bytes:** Fixes "unexpectedly closed" for https://device:8765 and wss:// (no truncation).
- **WSS/WS tab always on:** Tab after WiFi for all companion builds; label WSS or WS by build.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.17/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.17/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.17/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.17/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.16 — 2026-02-24

**Firmware version:** v1.14.0.16 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS handshake non-blocking:** mbedtls_ssl_set_bio recv_timeout = NULL so TLS handshake never blocks; fixes ERR_CONNECTION_RESET when opening https://deviceIP:8765 (cert page loads).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.16/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.16/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.16/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.16/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.15 — 2026-02-24

**Firmware version:** v1.14.0.15 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS cert page:** Opening https://deviceIP:8765 in a browser now returns 200 OK + HTML so you can accept the self-signed cert, then use wss:// from the client.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.15/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.15/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.15/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.15/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.14 — 2026-02-24

**Firmware version:** v1.14.0.14 (meshcomod on upstream 1.14+).

**Highlights:**
- **UI first in loop():** Run `ui_task.loop()` at start of every `loop()` so version screen dismisses at 3s even if mesh/serial blocks; fixes stuck version screen.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.14/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.14/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.14/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.14/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.13 — 2026-02-24

**Firmware version:** v1.14.0.13 (meshcomod on upstream 1.14+).

**Highlights:**
- **TCP/WSS defer 5s:** TCP and WSS server start deferred 5s after boot so version screen dismisses at 3s and USB works; fixes stuck version screen and GetContacts/GetChannel timeouts. WSS still at 10s + WiFi.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.13/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.13/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.13/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.13/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.12 — 2026-02-24

**Firmware version:** v1.14.0.12 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS boot fix:** mbedTLS init moved from global constructor to `begin()` (10s after boot, WiFi up). Fixes device stuck on "Loading..."; splash and home screen now appear.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.12/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.12/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.12/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.12/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.11 — 2026-02-24

**Firmware version:** v1.14.0.11 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS status tab:** New tab after WiFi/TCP shows WSS running or not, port, client count; when not running, explains 10s-after-WiFi start.
- WSS (TLS) on port 8765 restored; non-blocking. Build script and v1.14.0.10 unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.11/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.11/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.11/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.11/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.10 — 2026-02-24

**Firmware version:** v1.14.0.10 (meshcomod on upstream 1.14+).

**Highlights:**
- **Build script improvements:** `build-firmware <target>` no longer wipes `out/`; only bulk builds clear it so V4 then V3 bins accumulate. Platform detection for ESP32 mergebin/copy fixed (pio pipe + ESP32 fallback when bootloader/partitions present).
- Same firmware as v1.14.0.9 (plain ws:// port 8765; GetContacts over WiFi fix). WSS removed.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.10/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.10/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.10/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.10/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.9 — 2026-02-24

**Firmware version:** v1.14.0.9 (meshcomod on upstream 1.14+).

**Highlights:**
- **GetContacts over WiFi/WebSocket fixed:** WebSocket server-to-client frames with payload length ≥126 bytes now use RFC 6455 extended length encoding (2-byte **big-endian** length). The previous little-endian encoding caused browsers to drop or garble CONTACT (148-byte) and other large frames; START/END (5-byte) and small channel frames (<126) were unaffected.
- v1.14.0.8 behavior unchanged (optional WS frame debug, binary-only stream, reply-target pinning).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.9/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.9/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.9/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.9/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.8 — 2026-02-24

**Firmware version:** v1.14.0.8 (meshcomod on upstream 1.14+).

**Highlights:**
- **Optional WS frame debug:** Build with `-DWS_FRAME_DEBUG=1` to log each WebSocket send (client id, frame code 2=START/3=CONTACT/4=END, len, written). Use WiFi-only when enabled; see `src/helpers/esp32/DEBUG_WS_FRAME.md` for diagnosing GetContacts over WiFi.
- v1.14.0.7 behavior unchanged (binary-only companion stream, reply-target pinning).

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.8/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.8/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.8/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.8/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.7 — 2026-03-06

**Firmware version:** v1.14.0.7 (meshcomod on upstream 1.14+). **Build date:** 06 Mar 2026.

**Highlights:**
- **Companion stream binary-only:** Removed contact-list Serial.printf from companion path. USB/WS/TCP now carry only framed protocol bytes; no ASCII debug in same stream, so parser is not contaminated. GetContacts and Web Serial should load contacts reliably.
- Retry and reply-target fix (v1.14.0.6) unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.7/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.7/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.7/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.7/Heltec_v3_companion_radio_usb_tcp.bin) |

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
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.6/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.6/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.6/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.6/Heltec_v3_companion_radio_usb_tcp.bin) |

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
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.5/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.5/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.5/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.5/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.4 — 2026-02-24

**Firmware version:** v1.14.0.4 (meshcomod on upstream 1.14+).

**Highlights:**
- **WiFi WebSocket companion parity:** Contact list over `ws://<device-ip>:8765` now completes reliably. Retries for CONTACT/END frame writes so transient buffer full no longer drops the list; WebSocket/TCP no longer disconnect on first write failure so companion layer can retry. Same protocol as serial/TCP (binary frames, START → N×CONTACT → END).
- All v1.14.0.3 behavior (WebSocket server, Sync-Since) unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.4/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.4/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.4/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.4/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.3 — 2026-02-24

**Firmware version:** v1.14.0.3 (meshcomod on upstream 1.14+).

**Highlights:**
- **WebSocket server for browser-only WiFi:** When TCP is on and WiFi is up, device also listens on port **8765**. Connect with `ws://<device-ip>:8765` for device + browser only (no bridge). Status shows `ws: 8765` or `ws: off`.
- Sync-Since (62/61) and v1.14.0.2 behavior unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.3/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.3/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.3/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.3/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.2 — 2026-02-25

**Firmware version:** v1.14.0.2 (meshcomod on upstream 1.14+).

**Highlights:**
- **Sync-Since (for future custom client):** Command **62** (SyncSince) + response **61** (SyncSinceDone) for backfill after reconnect. Custom client must send 62 (not 60) and handle 61; stock clients unchanged.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.2/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.2/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.2/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.2/Heltec_v3_companion_radio_usb_tcp.bin) |

---

## v1.14.0.1 — 2026-02-24

**Firmware version:** v1.14.0.1 (meshcomod on upstream 1.14+).

**Highlights:**
- Boot splash: MeshCore logo removed; "meshcomod" text shown again as main title.

**Prebuilt binaries (use [flasher.meshcomod.com](https://flasher.meshcomod.com) — Easy mode auto-downloads versions; for manual upload, use Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.1/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.1/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.1/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.1/Heltec_v3_companion_radio_usb_tcp.bin) |

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
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.0/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.0/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.14.0.0/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.14.0.0/Heltec_v3_companion_radio_usb_tcp.bin) |

---

*For each new release, follow the [Release process](#release-process-do-this-for-every-new-fixrelease) above. Do not overwrite existing version folders.*
