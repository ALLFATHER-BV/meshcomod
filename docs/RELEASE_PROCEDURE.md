# Release procedure (companion firmware)

Use this for every new fix or release. **Rule: every release gets a new version number; never overwrite an existing version’s binaries.**

Current latest version in the repo: **v1.15.0.1** (bump to next for the following release).

---

## 1. Bump version

- Choose the next version (e.g. `v1.14.0.20`).
- In **`examples/companion_radio/MyMesh.h`**, update the fallback:
  ```c
  #define FIRMWARE_VERSION "v1.14.0.20"
  ```

---

## 2. Build companion targets

From **`MeshCore/`**:

```bash
export FIRMWARE_VERSION=v1.14.0.20

sh build.sh build-firmware heltec_v4_companion_radio_usb_tcp
sh build.sh build-firmware Heltec_v3_companion_radio_usb_tcp
sh build.sh build-firmware heltec_v4_tft_companion_radio_usb_tcp_touch
sh build.sh build-firmware Heltec_Wireless_Paper_companion_radio_usb_tcp
```

- Builds go to `out/`. For ESP32 meshcomod **`companion_radio_usb_tcp*`** envs, **`build.sh` also runs `mergebin`** and copies **`…-merged.bin`** next to the app-only bin (same flow for OLED V4, V3, and TFT+touch V4).
- Each `build-firmware` run adds its bins to `out/` (the script does not clear `out/` for `build-firmware`).

### Companion-only release

You **do not** have to build or copy TCP repeater firmware for every companion release.

- **Skip** repeater **`build.sh build-repeater-firmwares`** and **`scripts/copy-repeater-release-bins.sh`**.
- **`prebuilt/heltec_v4_repeater_tcp.bin`** (and friends) stay as they were until the next repeater promotion.
- **`prebuilt/releases/vX.Y.Z.W/`** may contain **only** companion binaries until a later repeater copy adds repeater files to that folder (or you ship repeater under a **different** `v*` — see [`REPEATER_RELEASE_PROCEDURE.md`](REPEATER_RELEASE_PROCEDURE.md)).

Agent-oriented checklist: **[`AGENTS.md`](../AGENTS.md)**.

---

## 3. Copy release binaries

```bash
sh scripts/copy-release-bins.sh v1.14.0.20
```

- Copies the companion binaries from `out/` into:
  - **`prebuilt/`** (latest **companion** stable names only)
  - **`prebuilt/releases/companion/v1.14.0.20/`** (versioned)
- Script expects versioned filenames in `out/` (e.g. `heltec_v4_companion_radio_usb_tcp-v1.14.0.20-...bin`).

**TCP repeater** (optional **`repeater/r*…/`** folder, parallel train to companion **`v*`**): see [`REPEATER_RELEASE_PROCEDURE.md`](REPEATER_RELEASE_PROCEDURE.md) — **`REPEATER_FIRMWARE_VERSION=…-repeater-tcp`**, **`copy-repeater-release-bins.sh`**. Repeater and companion **latest** roots under **`prebuilt/`** are updated by **different** scripts.

---

## 4. Update docs

- **`prebuilt/releases/companion/v1.14.0.20/notes.md`**  
  Add release notes (highlights, fixes, build date).

- **`RELEASES.md`**  
  Add a new section at the **top** (below “Release process”):
  - Version and date
  - Highlights (bullet list)
  - Table with links to the binaries in `prebuilt/releases/companion/v1.14.0.20/` (V4 OLED, V4 TFT+touch, V3, Wireless Paper — merged + non-merged where applicable)

Use the existing v1.14.0.19 block in `RELEASES.md` as the template (heading, **Firmware version**, **Highlights**, **Prebuilt binaries** table).

Optional sanity check (after **`notes.md`** mentions binaries):

```bash
sh scripts/validate-prebuilt-release-folder.sh companion v1.14.0.20
```

---

## 5. Commit and push

- Stage: prebuilts, `RELEASES.md`, `notes.md`, and any code (e.g. `MyMesh.h`).
- Commit with a clear message (e.g. `Release v1.14.0.20 – WSS fixes`).
- Push to **allfather**:  
  `git push allfather main`

---

## Summary

| Step | Action |
|------|--------|
| 1 | Bump version in `examples/companion_radio/MyMesh.h` |
| 2 | `export FIRMWARE_VERSION=vX.Y.Z.W` then build V4, V3, V4 TFT+touch, and Wireless Paper companions with `build.sh` |
| 3 | `sh scripts/copy-release-bins.sh vX.Y.Z.W` |
| 4 | Add `prebuilt/releases/vX.Y.Z.W/notes.md` and new section in `RELEASES.md` |
| 5 | Commit and `git push allfather main` |

- **`prebuilt/`** = rolling “latest” per **product** (companion copy overwrites companion stable names; repeater copy overwrites repeater stable names — independent).
- **`prebuilt/releases/<version>/`** = one folder per version (bins + `notes.md`); keep all for rollback. A folder may start **companion-only** and gain repeater bins later, or repeater may ship under a **different** `v*` than the newest companion.

---

## Repeater TCP prebuilts

Repeater TCP prebuilts ship under the **same `v1.14.1.x`** folders as companion (see **[`REPEATER_RELEASE_PROCEDURE.md`](REPEATER_RELEASE_PROCEDURE.md)** and meshcomod-client **`firmware_catalog.ts`**).

---

## Optional: GitHub Actions (other firmware)

For **companion / repeater / room-server** firmware built by GitHub Actions, push a tag:

- `companion-v1.0.0`
- `repeater-v1.0.0`
- `room-server-v1.0.0`

A draft GitHub Release is created; update the release notes and publish it. See **`RELEASE.md`** in the repo root.
