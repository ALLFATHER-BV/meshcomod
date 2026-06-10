# Meshcomod Touch — beta_12

A big **feature + polish** drop for **both boards** (Heltec V4 TFT and LilyGo
T-Deck / T-Deck Plus), built on MeshCore 1.16.0. Highlights: the **V4 is ~3×
snappier**, a **map crash is fixed**, **accent / extended-Latin typing**,
**mobile-style text selection & editing**, **region scopes**, and a stack of
**keyboard + landscape** fixes.

> ⚠️ **Over-the-air (Wi-Fi) self-update is still paused** while OTA slot-sizing is
> sorted. The **"update available" badge still works**; the button points you at
> the manual flasher. See *Updating* below.

---

## ⬆️ Updating from beta_8 / 9 / 10 / 11 — manual, app-only, no identity reset

Same partition layout as beta_8–11, so this is a **plain app-only update** — no
USB reflash, no identity/contacts reset:

- **T-Deck under Launcher:** reinstall the app-only bin (`meshcomod-tdeck.bin`).
- **USB / web flasher:** **flasher.meshcomod.com** → install the app-only image.

Coming from **beta_6 or earlier**, or a fresh device? Do the one-time
**merged-image USB install** (`*-merged.bin` at `0x0`, Erase first). The bins ship
**no recovery firmware** (T-Deck merged = clean bootloader + dual-OTA app).

---

## ✨ New

- **The V4 is much snappier.** It had silently been running at **80 MHz** — the
  code that was meant to raise the clock used an invalid frequency, so the call
  failed and it stayed slow. It now runs at **240 MHz**: popups, tab switches and
  the keyboard all feel noticeably faster. (T-Deck was already at 240 MHz.)
- **Accent / extended-Latin input** (issue #22). Type a letter that has accented
  forms (a, e, i, o, u, n, c, s, z …) and a little **pop-up of variants**
  (à á ä â …) appears — tap one to drop it in. Works on the T-Deck's physical
  keyboard and the V4 on-screen keyboard.
- **Region scope** (issue #18). **Settings → Radio → "Region scope"**: enter your
  region's `#hashtag` and outgoing flood packets are tagged with that region, so
  repeaters on **region-flood-only** networks will carry your traffic. Blank =
  unscoped (the previous behaviour).
- **The chat box grows as you type.** Long messages now **wrap onto extra lines**
  and the box grows upward (up to 4 lines, then scrolls) instead of scrolling
  sideways on one line. Enter still sends.
- **Text selection & editing.** **Double-tap a word** to select it; **long-press**
  for a **Cut / Copy / Paste / Select-All** menu (shares the clipboard with
  long-press-to-copy on chat bubbles, so you can copy a received message and paste
  it into the composer). **Backspace** deletes the whole selection.
- **T-Deck: just start typing.** Opening a chat (or a pop-up with a text field)
  now puts the cursor in the box automatically — no tap needed before the physical
  keyboard types.
- **Home screen** shows live **RAM% / PSRAM%** (matching the control-center
  sysinfo) in place of the old duty-cycle line.
- **Control center:** the brightness slider moved up, **above** the GPS line.

---

## 🐞 Fixed

- **Map no longer panics while panning.** A slow/stalled Wi-Fi **tile download**
  could block long enough to trip the task watchdog and reboot the device. The
  fetch now bounds its network waits below the watchdog and yields while
  downloading, so flaky tiles just retry instead of crashing.
- **On-screen keyboard — English layout restored.** A multi-language rework had
  swapped English for a hand-rolled map with a **broken shift** (it typed `^`) and
  an odd Close/cursor-arrow bottom row. English is back to the **stock layout**:
  proper shift, `1#` symbols, a wide spacebar, key-press preview.
- **Landscape keyboard fixes.** The rotate / language buttons and the accent
  pop-up no longer **overlap the keys**, and the **chat composer now fills the
  width** of the screen (and shows immediately) after rotating — previously it
  stayed portrait-width and only appeared once you reached a second line.
- **Readable text selection** — selected text now uses a high-contrast highlight
  (it was unreadable against the dark theme), and it **stays highlighted** while
  the Cut/Copy/Paste menu is open.
- **Home info separator** rendered as a tofu box; it now uses the extended font.
- Removed the redundant on-screen **"á" key** — the automatic accent pop-up
  replaces it.

---

## Flash

| Board | Fresh / from ≤ beta_6 (USB @ `0x0`, **Erase** first) | Update from beta_8+ (app-only @ `0x10000`) |
|-------|------------------------------------------------------|--------------------------------------------|
| **Heltec V4 TFT** | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` | `heltec_v4_tft_companion_radio_usb_tcp_touch.bin` |
| **LilyGo T-Deck** | `LilyGo_TDeck_companion_radio_touch-merged.bin` | `LilyGo_TDeck_companion_radio_touch.bin` |

Easiest path is the web flasher at **flasher.meshcomod.com** (rolling
`meshcomod-tdeck` / `meshcomod-heltec-v4-tft` names), or grab the versioned bins
from `prebuilt/releases/TOUCH/beta_12/`.

NVS-preserving USB flash (keeps Wi-Fi creds) writes the four parts individually
(`0x0` bootloader, `0x8000` partitions, `0xe000` boot_app0, `0x10000` firmware) —
the merged image at `0x0` is only for the deliberate fresh install (Erase + write).
