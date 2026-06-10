# Meshcomod Touch — beta_14

A **personalisation + polish** drop for both boards (Heltec V4 TFT and LilyGo
T-Deck / T-Deck Plus): make it yours with a theme colour and colourful chat
bubbles, a per-channel region scope, and a handful of quality fixes.

> ⚠️ **Over-the-air (Wi-Fi) self-update is still paused.** The "update available"
> badge still works; the button points you at the manual flasher.

---

## ⬆️ Updating from beta_8–13 — app-only, no identity reset

Same partition layout, so this is a **plain app-only update** — no USB reflash,
no identity/contacts reset:

- **T-Deck under Launcher:** reinstall the app-only bin (`meshcomod-tdeck.bin`).
- **USB / web flasher:** **flasher.meshcomod.com** → install the app-only image.

Fresh device, or coming from **beta_6 or earlier**? Do the one-time merged-image
USB install (`*-merged.bin` at `0x0`, **Erase** first).

---

## ✨ New

- **Theme colour.** *Settings → Theme colour* (or the new **Theme** chip in the
  pull-down control center): pick an accent from a swatch grid or type a `#hex`
  code. Whatever you choose is auto-darkened just enough to keep text readable,
  and it recolours the whole UI on a quick restart.
- **Colourful chat bubbles** (on by default). Each sender gets a stable colour
  derived from their name — a dark tinted bubble + a vivid name line — so you can
  tell people apart at a glance in a group. Toggle it in Settings (turning it on
  says *"taste the rainbow"*).
- **Per-channel region scope.** Open a channel and tap the **⚙** that now appears
  to the left of the name in the top bar → set a `#region` scope **just for that
  channel**, overriding the global one. Blank = inherit the global scope.

---

## 🐞 Fixed

- **Offline map zoom.** With map tiles on a microSD card, the zoom buttons only
  checked the internal cache and reported *"Max zoom for this pack"* offline. The
  zoom check now reads the same SD pack the map draws from, so SD packs zoom fully
  offline. (Thanks christianprim.)
- **Cleaner serial console.** The companion firmware no longer pushes binary
  protocol frames onto the USB serial until an app actually connects — a plain
  serial monitor stays quiet instead of filling with junk.
- **Control center:** the GPS line no longer overlaps the toggle buttons, and
  swiping while a full-screen picker (theme / channel scope) is open no longer
  flips the tab behind it.

---

## Flash

| Board | Fresh / from ≤ beta_6 (USB @ `0x0`, **Erase** first) | Update from beta_8+ (app-only @ `0x10000`) |
|-------|------------------------------------------------------|--------------------------------------------|
| **Heltec V4 TFT** | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` | `heltec_v4_tft_companion_radio_usb_tcp_touch.bin` |
| **LilyGo T-Deck** | `LilyGo_TDeck_companion_radio_touch-merged.bin` | `LilyGo_TDeck_companion_radio_touch.bin` |

Easiest path is the web flasher at **flasher.meshcomod.com** (rolling
`meshcomod-tdeck` / `meshcomod-heltec-v4-tft` names), or grab the versioned bins
from `prebuilt/releases/TOUCH/beta_14/`.

NVS-preserving USB flash (keeps Wi-Fi creds) writes the four parts individually
(`0x0` bootloader, `0x8000` partitions, `0xe000` boot_app0, `0x10000` firmware) —
the merged image at `0x0` is only for the deliberate fresh install (Erase + write).
