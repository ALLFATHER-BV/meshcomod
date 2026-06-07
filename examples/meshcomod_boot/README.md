# meshcomod_boot

An on‑device **recovery / backup / firmware‑switch** tool for the LilyGo T‑Deck
(ESP32‑S3, 16 MB) — think *bootloader + TWRP recovery + nandroid backups* for a mesh
radio. Clean‑room (not derived from any existing launcher); see
**[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** for the design and how it differs from
[bmorcelli/Launcher](https://github.com/bmorcelli/Launcher).

Built on the Arduino framework, **reusing the `Meshcomod_Touch` T‑Deck UI base**
(Adafruit ST7789 display + the `TDeck*` trackball/keyboard drivers).

## Status

| Capability | State |
|---|---|
| Full‑flash **backup** to SD (16 MB image + SHA‑256 manifest) | ✅ working, verified bit‑perfect on hardware |
| On‑screen **menu** (trackball + keyboard) | ✅ Backup / Device info / Restore browser / Reboot‑to‑bootloader |
| Backup **browser + verify** (recompute SHA‑256 vs manifest) | ✅ working on device |
| **Full‑clone restore / firmware install** (computer) | ✅ `restore.sh` (esptool) — see below |
| **On‑device firmware switch** (Meshtastic ↔ Meshcomod) | 🔜 OTA‑slot architecture (next) |

## Build & flash

This is now part of the **Meshcomod** firmware repo. Build it from the repo
root as the `LilyGo_TDeck_recovery` env — a standalone Arduino app that does not
pull in the MeshCore core:

```bash
pio run -e LilyGo_TDeck_recovery                 # build
pio run -t mergebin -e LilyGo_TDeck_recovery     # full image (flash @ 0x0)
```

The env is in `variants/lilygo_tdeck_recovery/platformio.ini`; sources are here
under `src/`. (The local `platformio.ini` in this folder still works for
standalone dev — `cd` here, then `pio run -e tdeck` — but the repo build above
is canonical.)

First flash of a *fresh* device may need manual download mode (hold trackball +
power‑cycle); after that, auto‑reset just works.

## Restore options

- **On device, no computer:** boot the recovery menu → *Restore from SD* → pick a backup
  → it verifies the image's SHA‑256 against its manifest. (Slot‑based *write‑back* switch
  is the next milestone.)
- **From a computer (full clone / install any firmware):** a complete 16 MB restore — or
  installing a stock full‑flash firmware image — overwrites the partition the recovery runs
  from, which isn't possible on‑device. Use the host script (the backup is a standard
  esptool image):
  ```bash
  bash restore.sh "/Volumes/NO NAME/BKUP0006.IMG" /dev/cu.usbmodem101
  ```

> Companion UI firmware: the **LVGL touch UI** in this same repo
> (`examples/companion_radio/ui-touch/`), which runs from the `ota_0` slot while
> this recovery lives in `factory`.
