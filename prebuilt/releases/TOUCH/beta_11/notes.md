# Meshcomod Touch — beta_11

A **stability + bug-fix** drop for **both boards** (Heltec V4 TFT and LilyGo
T-Deck / T-Deck Plus). Built on MeshCore 1.16.0. Several of these came straight
out of hardware-in-the-loop testing under bmorcelli **Launcher**.

> ⚠️ **Over-the-air (Wi-Fi) self-update is paused in this release** while we sort
> out OTA partition-slot sizing. The **"update available" check still works** —
> you'll still get the badge — but the button now tells you to **update
> manually**. See *Updating* below.

---

## ⬆️ Updating from beta_8 / 9 / 10 — manual, no identity reset

Same partition layout as beta_8–10, so this is a **plain app-only update** — no
USB reflash, no identity/contacts reset:

- **T-Deck under Launcher:** reinstall the app-only bin (`meshcomod-tdeck.bin`)
  via Launcher.
- **USB / web flasher:** **flasher.meshcomod.com** → install the app-only image.

Coming from **beta_6 or earlier**, or doing a fresh device? Do the one-time
**merged-image USB install** (`*-merged.bin` at `0x0`) first.

The bins ship **no recovery firmware** (T-Deck merged = clean bootloader +
dual-OTA app).

---

## 🐞 Fixed — both boards

- **Chat history is no longer lost under Launcher.** The history save assumed an
  internal SPIFFS partition; under Launcher (which has none) it spammed
  `SPIFFS: spiffs partition could not be found` every ~2 s **and never persisted
  the chat**. It now writes to the active storage (the SD card under
  `/meshcomod`) — the log spam is gone and your chats survive a reboot.
- **"Found" list no longer shows garbage/corrupt entries.** The Discovered ring
  buffer was allocated from uninitialised PSRAM, so random slots looked "used"
  and rendered as junk contacts. The allocator now zero-initialises — Found shows
  only real discovered nodes (and adverts are no longer buried among the junk).
- **Network tab no longer locks up.** Saving a Wi-Fi "quick-connect" profile slot
  used to leave the page stranded behind an invisible overlay (Close stopped
  working — a reboot was the only way out). The row now refreshes in place.
- **The "auto-add chats" toggle finally works.** Person/chat adverts were always
  auto-added to Contacts regardless of the setting. Now, with auto-add **off**,
  they go to the **Found** list to be added by hand (repeater/room/sensor already
  honoured this). *Note:* a brand-new sender's first DM can't be decoded until you
  add them — add from Found first, or turn the chats toggle back on.
- **Discovered modal layout** — cards are full-width now and long names no longer
  overlap the type/hops line.
- **OTA button → manual update.** Wi-Fi self-update is disabled for now; the
  button is relabelled **"How to update"** and points you at the flasher.

---

## Flash

| Board | Fresh / from ≤ beta_6 (USB @ `0x0`) | Update from beta_8+ (app-only @ `0x10000`) |
|-------|--------------------------------------|--------------------------------------------|
| **LilyGo T-Deck** | `LilyGo_TDeck_companion_radio_touch-merged.bin` | `LilyGo_TDeck_companion_radio_touch.bin` (or `meshcomod-tdeck.bin`) |
| **Heltec V4 TFT** | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` | `heltec_v4_tft_companion_radio_usb_tcp_touch.bin` (or `meshcomod-heltec-v4-tft.bin`) |

---

## Known issues / next

- **OTA over Wi-Fi disabled** (this release) — manual update only, by design.
- Auto-add-on-DM (auto-add a sender the instant their DM decodes, keeping Found
  clean without dropping cold DMs) is in progress.
- Non-Latin accented names (e.g. German `ö/ü/ä`) still render as boxes — a font
  coverage fix (Latin-1 Supplement) is queued.

**Status:** beta — daily-usable, actively tested on real hardware.
