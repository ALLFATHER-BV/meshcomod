# Meshcomod Boot — Architecture & Rationale

> A clean-room, from-scratch **bootloader + recovery + image manager** for the
> LilyGo T-Deck (ESP32-S3, 16 MB). Not derived from any existing launcher's code.
> This document exists to make one thing clear: **why a separate project, and what
> it does structurally differently** from the obvious prior art
> ([bmorcelli/Launcher](https://github.com/bmorcelli/Launcher)).

---

## 0. Thesis (the one-liner)

bmorcelli/Launcher is an **app launcher**: a resident app that *adapts each firmware's
app section into a partition layout it manages*. It is broad (dozens of boards) and
mutate-in-place.

Meshcomod Boot is a **recovery + image system**: an immutable boot core that treats each
firmware as an **opaque, verifiable, whole-flash image (a "profile")**, swapped
**atomically and reversibly**, with the device's **entire state snapshot-able to SD**.

In Android terms: bmorcelli ≈ a fancy app drawer. Meshcomod Boot ≈ **bootloader + TWRP
recovery + nandroid backups + fastboot images**, scoped tightly to the T-Deck and a
small set of curated profiles (Meshcomod ↔ Meshtastic ↔ stock).

That reframing — *image/profile + recovery* instead of *app launcher* — is the reason
this is a different project, not a fork.

---

## 1. Goals / non-goals

**Goals**
- **Whole-device backup** to SD: bit-exact 16 MB image (bootloader, partition table,
  apps, filesystems, NVS) — a true "nandroid".
- **Restore** any backup or curated profile from SD, with **no computer**.
- **Switch firmwares** (Meshcomod/MeshCore ↔ Meshtastic ↔ others) from SD, preserving
  *each* firmware's own user data across switches.
- **Brick-proof by design**: power-loss mid-flash, a bad image, or a corrupt app
  partition must always land you back in recovery — not a paperweight.
- **Accept stock release images unmodified** — no "repackaged for our launcher" step.
- Polished **LVGL touch UI**, consistent with Meshcomod Touch, with correct handling of
  the T-Deck's *shared TFT/SD SPI bus*.

**Non-goals**
- Not a universal multi-board launcher. **T-Deck-first**, opinionated, curated.
- Not an app store. A handful of trusted profiles, not "install anything".
- Not trying to run two full firmwares resident at once (16 MB won't allow it — see §5).

---

## 2. Why not just fork bmorcelli/Launcher

Studied at the behavior/architecture level only (we do **not** reuse its code). Findings:

| Dimension | bmorcelli/Launcher | Meshcomod Boot (this project) |
|---|---|---|
| Mental model | App launcher; adapts firmware **app section** into a managed layout | **Image/profile** manager; firmware is an opaque whole-flash image |
| Boot mechanism | Resident **app** → `esp_ota_set_boot_partition()` | **Custom 2nd-stage bootloader hook** decides *before* any app runs |
| Recovery reachability | None at boot stage; if the app slot is bad, you're stuck | **Key-combo / failed-boot → recovery**, decided in the bootloader |
| Backup/restore | **Data partitions only** (SPIFFS/FAT) | **Whole-flash nandroid** + per-profile userdata snapshots |
| Stock binaries | Often needs **"adjusted"/Launcher-only** repackaged bins; reconstructing a partition scheme from an arbitrary bin is an **open problem** ([#249](https://github.com/bmorcelli/Launcher/issues/249)) | Accept **stock merged/full images**; the partition table travels *inside* the image — nothing to reconstruct |
| Power-fail safety | None documented; real **install bootloops** ([#244](https://github.com/bmorcelli/Launcher/issues/244)) | **Journaled, resumable** flashing; interrupted flash → recovery resumes/rolls back |
| Rollback | None | **A/B trial-boot + auto-rollback** (esp-idf anti-rollback) |
| Integrity/trust | Heuristic "sanity checker"; "wrong partition scheme" errors ([#132](https://github.com/bmorcelli/Launcher/issues/132)) | **Per-region SHA-256 manifest**, optional **signature** on curated images |
| Userdata across switches | App/data-centric; switching to a different layout clobbers state | **Per-profile userdata snapshot/restore** — each firmware keeps its contacts/keys/wifi |
| Scope | Universal (M5Stack, CYD, Marauder, Lilygo…) → lowest common denominator | **T-Deck-first** → can be opinionated and correct (e.g. shared SPI bus, [#228](https://github.com/bmorcelli/Launcher/issues/228)) |
| UI | ArduinoGFX/LovyanGFX (LVGL planned) | **LVGL**, reusing the Meshcomod Touch look/feel |

The throughline: bmorcelli's breadth forces it to *mutate the device's partitions to fit
each firmware*, which is exactly where its hard problems live (schema reconstruction,
wrong-scheme errors, install bootloops, data loss). By going **narrow + image-based +
recovery-backed**, those problems largely disappear.

---

## 3. The structural reframing

Two ideas do all the work:

1. **Firmware = an opaque whole-flash image ("profile").** We never try to understand a
   firmware's internal partitioning. A profile carries *its own* partition table and
   everything else as raw regions. Switching = writing those regions. This sidesteps
   bmorcelli's whole class of "reproduce the partition scheme" failures.

2. **Recovery is reachable from the bootloader, not from an app.** The decision "boot the
   firmware vs boot recovery" happens in a tiny custom 2nd-stage bootloader hook, *before*
   any app loads. So recovery survives a corrupt/half-flashed firmware slot. This is the
   difference between a *launcher* and a *recovery*.

---

## 4. Architecture (layers)

```
 ┌────────────────────────────────────────────────────────────┐
 │ ROM bootloader (immutable, in-chip)  ── USB DL mode backstop │
 ├────────────────────────────────────────────────────────────┤
 │ L0  2nd-stage bootloader + hook (tiny, headless)             │
 │     decides: active firmware  |  recovery   (intent/keycombo │
 │     /failed-boot/rollback). Reads boot journal.              │
 ├────────────────────────────────────────────────────────────┤
 │ L1  Recovery STUB (small resident app, headless-ish)         │
 │     brings up SD, chainloads the big recovery UI from SD     │
 │     into RAM/PSRAM and jumps to it. Fallback if SD missing.  │
 ├────────────────────────────────────────────────────────────┤
 │ L2  Recovery APP (lives on SD: /boot/recovery.bin, LVGL)     │
 │     backup · restore · switch profile · flash · verify ·     │
 │     partition inspector · factory reset · logs               │
 ├────────────────────────────────────────────────────────────┤
 │ L3  Image/Profile model on SD  (manifests + regions + data)  │
 ├────────────────────────────────────────────────────────────┤
 │ L4  Journaled flash engine + nandroid backup/restore         │
 └────────────────────────────────────────────────────────────┘
```

**L0 — Bootloader hook (the brick-proof core).** Built with ESP-IDF
`bootloader_hooks`/`bootloader_override`. Headless (the bootloader stage can't run LVGL
or the TFT). Its only job: read a boot-intent + journal from a tiny `bootstate` region,
read a GPIO key-combo (e.g. hold trackball / a keyboard key at power-on), and choose to
boot the **active firmware** or the **recovery stub**. If the last flash journal is
incomplete, or the active app failed its trial boot, it forces recovery. This is what
makes the device *unbrickable from software* short of corrupting the bootloader itself.

**L1 — Recovery stub.** A *small* resident app (easy to write, no UI framework). Mounts
SD, loads `/boot/recovery.bin` into PSRAM, verifies its SHA-256, and executes it from RAM.
Keeping the heavyweight UI on SD means: (a) profiles can own almost the entire flash, and
(b) the recovery UI is updated by replacing one file on the SD card. If SD or the file is
missing/bad, the stub shows a minimal error (serial + a static framebuffer splash) and
offers "format/repair SD" or "wait for USB".

**L2 — Recovery app.** The "TWRP". Full LVGL touch UI (reuse Meshcomod Touch widgets and,
crucially, its *single-owner SPI bus discipline* so TFT and SD don't fight — a documented
T-Deck pain point). All user-facing operations live here.

**L3 — Image/Profile model (on SD).** A profile is a directory:
```
/profiles/meshtastic-2.x/
  manifest.json        # name, version, board, flash_size, regions[], userdata[]
  bootloader.bin       # region @ 0x0
  partitions.bin       # region @ 0x8000
  app.bin              # region @ <profile's app offset>
  fs-littlefs.bin      # region(s) for the profile's own filesystem(s)
  ...
```
`manifest.json` lists every region `{offset, size, file, sha256}` plus which partitions are
**userdata** (nvs, spiffs, littlefs) to snapshot on the way out and restore on the way in.
Two profile kinds:
- **Full image** — regions cover the whole 16 MB → an exact clone, partition-table-agnostic.
  This is how we ingest *stock* Meshtastic/MeshCore releases (from their published offsets,
  a merged factory.bin, or a raw 16 MB dump) **without repackaging**.
- **Slot image** — app-only, for profiles that opt into a shared table → fast OTA-style swap.

**L4 — Journaled flash engine + nandroid.**
- *Backup (nandroid):* `esp_flash_read` the full 16 MB to `/backups/<label>.img` + a
  manifest of per-region SHA-256. Read-only, always safe.
- *Restore / switch:* write regions as a **journaled transaction** — record intent in
  `bootstate`, write region, **read-back-verify SHA-256**, advance journal, repeat, then
  reboot. Power loss at any point → L0 sees the open journal → boots recovery → recovery
  *resumes or rolls back* from the SD source. Restoring regions that include the recovery
  stub or bootloader uses the **RAM-stub trick** (a tiny IRAM writer finishes the job and
  resets — same principle esptool uses to reflash everything incl. `0x1000`/`0x8000`).
- *Rollback:* after switching, mark the new firmware **trial**; if it doesn't boot cleanly
  (watchdog / no check-in within N boots), L0 auto-reverts to the previous profile.

---

## 5. Flash / partition strategy

The T-Deck's 16 MB is *already fully used* by a single firmware (see Meshcomod Touch's
`partitions_tdeck_touch.csv`: app0 6.25 M / app1 1.5 M / tiles 4.75 M / spiffs 3.375 M).
**Conclusion: you cannot keep two full firmwares resident.** The model is therefore
**SD-backed**: flash holds *one* active firmware (its own native table), SD holds the
profiles and backups, and we flash on demand.

The open tension: a *stock* full-flash profile wants the entire 16 MB, leaving no room for
a resident recovery. Three ways to resolve it, in preference order:

- **(A) SD-resident recovery (preferred).** Flash holds only: bootloader + hook, a tiny
  `bootstate`/journal region, a small **recovery stub**, and the **active firmware**. The
  big recovery UI lives on SD. Cost: the stub must reserve a *small* fixed region that
  every profile's table agrees to leave free (a few hundred KB at top-of-flash). Stock
  images are trimmed by exactly that reserved tail when ingested (a deterministic,
  losslessly-reversible adjustment — not a per-firmware repack).
- **(B) Reserved recovery partition.** A ~2 MB resident recovery app; profiles must fit the
  remaining ~14 MB. Simpler, but reintroduces a light repack and shrinks the firmware budget.
- **(C) Pure resident, no bootloader hook.** Falls back toward the bmorcelli model; we lose
  the brick-proof property. Rejected except as an early throwaway prototype.

We design for **(A)**, keep **(B)** as the pragmatic fallback if the bootloader-stage
SD/RAM-load spike (see §8) proves too costly.

---

## 6. Key flows

- **Switch to profile X:** snapshot current firmware's userdata → SD; open journal; write
  X's regions (table, bootloader if needed, app, fs); restore X's saved userdata (if any);
  mark trial; reboot. Crash-safe at every step.
- **Backup:** dump 16 MB + per-region hashes to SD. Optionally also export userdata-only.
- **Restore image Y:** same as switch, but Y is a full nandroid image (exact clone).
- **Recover from interrupted flash:** automatic — L0 sees open journal, boots recovery,
  recovery resumes from the SD source or rolls back.
- **Enter recovery anytime:** hold the key-combo at power-on (works even if firmware is dead).

---

## 7. Honest constraints, risks, and the un-brick backstop

- **Bootloader-stage UI is impossible.** L0 is headless; all UI is in L1/L2. Accepted.
- **The two genuinely hard spikes** (must de-risk first): (i) a custom bootloader that can
  *load and run an app from SD/RAM*; (ii) executing the final restore writer **from RAM** so
  we can overwrite our own stub/bootloader. Both are proven *in principle* (esp-idf custom
  bootloader examples; esptool's RAM stub) but are the riskiest parts — prototype them
  before committing to (A).
- **NVS = identity.** A bit-exact restore re-clones private keys + wifi. Great for "restore
  *my* device"; cloning an image to a *different* unit duplicates identities — surface this
  in the UI.
- **Shared TFT/SD SPI bus** on T-Deck must be arbitrated carefully (single owner, explicit
  CS, no concurrent access) — a known source of SD-not-detected failures.
- **Ultimate backstop:** corrupting the bootloader region itself is the only true brick, and
  even then the ESP32-S3 **ROM USB download mode** (BOOT button) reflashes over USB. So:
  *unbrickable without a computer in all software-failure cases; unbrickable with a USB
  cable in all cases.* Keep an `esptool` full-restore recipe in the repo as the final net.

---

## 8. Stack decision (needs confirmation)

| Option | Pros | Cons |
|---|---|---|
| **ESP-IDF everything** | Native bootloader hooks, rollback, flash APIs; cleanest | LVGL-on-IDF UI work; can't directly reuse Arduino UI code |
| **Arduino everything** | Reuse Meshcomod Touch UI fast | No real custom bootloader → loses the core differentiator |
| **Hybrid (recommended)** | IDF base + custom bootloader hook for L0/L1; **arduino-esp32 as an IDF component** for the L2 LVGL UI → keep the brick-proof core *and* reuse UI | More build-system setup up front |

**Recommendation: Hybrid.** It's the only option that preserves the thing that makes this a
*recovery* (bootloader-stage control) while still reusing the Touch UI.

---

## 9. First milestone — de-risk before building

A throwaway spike that answers the two questions that can sink design (A):
1. Custom ESP-IDF bootloader hook on the T-Deck that reads a GPIO key-combo and boots one of
   two app partitions. (Proves bootloader-stage control.)
2. A resident stub that loads a second app image **from SD into PSRAM and executes it**.
   (Proves SD-resident recovery.)

If both work, commit to (A) and build L3/L4. If (2) is too costly, fall back to (B).

---

## 10. De-risking update (2026-06) — Spike 1 is mostly a *stock* feature

Confirmed against the ESP-IDF ESP32-S3 bootloader docs: the **stock** second-stage
bootloader already provides what L0 needs, so a hand-written custom bootloader override is
**not required for v1**. The three recovery-entry paths map to built-ins:

- **Emergency entry (GPIO):** `CONFIG_BOOTLOADER_APP_TEST` boots an alternate app partition
  (type `app`, subtype `test`) when a configured GPIO is held at boot
  (`CONFIG_BOOTLOADER_NUM_PIN_APP_TEST`, `…_PIN_LEVEL_LOW/HIGH`, `…_HOLD_TIME_GPIO`). This is
  the "firmware is bricked → still reach recovery" path.
- **Normal entry (intent flag):** an app selects recovery with `esp_ota_set_boot_partition()`
  + reboot — no GPIO, no custom bootloader.
- **Automatic entry (rollback):** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` +
  `esp_ota_mark_app_valid_cancel_rollback()` auto-reverts a firmware that fails to validate
  after a switch.

So **L0 collapses to: stock bootloader + sdkconfig + a tiny boot-intent helper.** A fully
custom override (splash screen at boot stage, or loading recovery from SD *in the
bootloader*) is **deferred** until/unless we outgrow the built-ins.

**Hardware caveat (T-Deck):** the only bootloader-readable button is the **trackball click
= GPIO0** (confirmed in `Meshcomod_Touch/src/helpers/input/TDeckTrackball.h`), which is also
the USB-download strap. Holding it *through* reset → ROM download mode, so the recovery
gesture is **"reset, release, then press & hold the trackball ≥5 s."** The keyboard is on
I²C and is invisible to the bootloader, so there's no spare hardware button.

**Spike 1** (`spikes/spike1_bootsel/`) validates the one part that still needs real silicon:
that `BOOTLOADER_APP_TEST` actually diverts the boot to the `test` partition on this board.
Spike 2 (SD→RAM chainload) follows only if we still want bootloader-stage SD recovery beyond
the stock built-ins.

---

## 11. Recovery entry, revised after studying the shipping firmware (2026-06)

The production Meshcomod (MeshCore) T-Deck firmware **already implements the recovery
primitives — at the app level**, which sidesteps the GPIO0/download-strap dead-end from §10.
We reuse these instead of any bootloader GPIO trigger:

- **Intentional entry — app-level early long-press.** `UITask::handleLongPress` already does
  *"long-press in the first 8 s after boot → `enterCLIRescue()`"* (`UITask.cpp:1136`). We mirror
  it: an early long-press (or a menu item) sets the boot partition to the recovery image and
  `esp_restart()`s. App-level GPIO0 reads are fine — only the *bootloader* can't use GPIO0.
- **USB recovery without a button combo — software force-download.** The firmware's
  `bootloader`/`dfu` CLI command sets `RTC_CNTL_OPTION1 |= RTC_CNTL_FORCE_DOWNLOAD_BOOT` then
  `esp_restart()`, dropping the ROM into USB download mode on the next reset
  (`MyMesh.cpp:515-538`). Recovery offers the same "enter download mode" action.
- **Boot selection — A/B OTA is already live.** The shipping table is two equal OTA slots
  (`ota_0`/`ota_1`, 3.875 MB each) and it self-updates via `startHttpOtaFromUrl` /
  `pollHttpOtaReboot`. So `esp_ota_set_boot_partition` + otadata are proven on this hardware.
- **Brick (firmware won't boot far enough to read the long-press):** A/B rollback
  (anti-rollback) reverts automatically; physical BOOT button → ROM download is the
  with-computer last resort.

**Net: L0 needs no custom bootloader and no GPIO app-test.** Recovery entry = app-level
long-press / menu → set-boot + reboot (a proven pattern on this device), with rollback +
software-force-download + USB as the safety net. Spike 1 still served its purpose (it proved
custom partition tables + bootloader-built-from-source + app-partition selection on real
silicon, and uncovered the GPIO0-strap fact). Hardware notes for the recovery app: drive
**GPIO10 (peripheral power) HIGH early**, and treat **TFT + LoRa + SD as one shared SPI bus**
(TFT SCLK/MOSI = 40/41, same pins as LoRa).

---

## 12. Restore feasibility — split into two paths (2026-06, after building backup)

On-device **full-chip self-restore is not feasible**, and it's a hard deadlock, not a
"try harder": writing flash requires the cache (XIP) disabled, but reading the next chunk
off the SD needs the FAT/SD driver, which *executes from the very flash being overwritten*.
For the recovery's own ~3 MB region there's no way out — it's too big to buffer in DRAM,
and reimplementing SD+FAT+flash entirely in IRAM is impractical. So restore splits:

- **Full-clone restore (whole 16 MB, incl. recovery)** → a *computer* job. The `BKUP*.IMG`
  we write is a standard esptool image, so `restore.sh` flashes it (or any stock full
  firmware image) at `0x0`. This is the ultimate un-brick + a way to install any firmware.
- **No-computer switch/restore** → the **OTA-slot model** (next build): recovery stays
  permanent in `factory`; firmwares live in `ota_0`; recovery writes the *slot*, never
  itself, so it's brick-safe. Firmwares must be co-built for the shared partition table.

This is consistent with §11: recovery is reached at the app level, lives in `factory`, and
operates on *other* partitions — never on the region it executes from.

---

*Prior art surveyed 2026-06: bmorcelli/Launcher, tobozo/M5Stack-SD-Updater, Espressif
multi-boot + custom-bootloader docs, esptool flasher-stub. None combine whole-flash
nandroid + bootloader-stage recovery + crash-safe image swap for the T-Deck; that
combination is this project's reason to exist.*
