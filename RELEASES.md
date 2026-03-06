# Release log

Versioned prebuilts are listed here so you can **roll back** if a newer release causes issues. The **latest** build is always in [`prebuilt/`](prebuilt/) (same files are updated on each release). For a specific version, use the links below.

## Release process (do this for every new fix/release)

**Rule: Every new fix or release gets a new version number. Never overwrite an existing version's binaries.**

1. **Bump version** — Choose the next version (e.g. `v1.14.0.1`). Do not reuse the current version.
2. **Build** — Build both Heltec V4 and V3 USB+TCP firmware: `export FIRMWARE_VERSION=v1.14.0.1` then `sh build.sh build-firmware heltec_v4_companion_radio_usb_tcp` and `sh build.sh build-firmware Heltec_v3_companion_radio_usb_tcp`.
3. **Create versioned folder** — Create `prebuilt/releases/<version>/` and copy the four prebuilt bins into it (and into `prebuilt/` as latest). E.g. run `sh scripts/copy-release-bins.sh v1.14.0.1`. Add `notes.md` in the version folder (often done in step 4).
4. **Update this file** — Add a new section above with the new version number, date, highlights, and table linking to the new release paths.
5. **Commit and push** — Commit the new/updated prebuilts and RELEASES.md, then push.

**Summary:** `prebuilt/` = latest only. `prebuilt/releases/<version>/` = one folder per version (with bins + `notes.md`); keep all of them so users can roll back.

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
