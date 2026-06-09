# Meshcomod Touch — beta_8

A focused follow-up to beta_7 that makes the **[Launcher](https://github.com/bmorcelli/Launcher)**
path work end to end — Wi-Fi map tiles and storage both behave under Launcher now —
plus a fix for the quick-replies editor and a smarter first-boot storage default.
Built on MeshCore 1.16.0.

---

## ⬆️ Updating from beta_7 — no reflash needed

beta_8 keeps the **same partition layout** as beta_7, so this one is a normal
**over-the-air / Launcher app update** — just install the **app-only** bin
(`…_touch.bin`). No USB merged-image reflash, no identity reset.

Coming from beta_6 or earlier? Do the one-time **merged-image USB install** as in
beta_7 first, then you're on OTA for good.

Like beta_7, **the bins ship no recovery firmware** — the T-Deck merged image is a
clean bootloader + dual-OTA app, with nothing at the old factory/recovery offset.

---

## ✨ New

### First boot defaults to the SD card (T-Deck)
- The very first time meshcomod boots on a fresh device, if a microSD card is
  present it now **automatically stores everything on the card** under
  `/meshcomod/` and turns *Settings → Device → Store data on SD* **on** for you —
  keeping internal flash free and matching what you want under Launcher.
- **Existing installs are never touched**: the auto-default only triggers on a
  genuinely fresh device (setup not yet completed *and* nothing saved on internal
  flash), so a device updated from an earlier build is never migrated onto an
  empty card.

### Wi-Fi map tiles now work under Launcher
- meshcomod caches downloaded map tiles to a dedicated "tiles" flash partition —
  which doesn't exist under Launcher's partition table, so the map used to show
  *"Map storage error."* Now, when that partition is absent, the Wi-Fi tile cache
  **falls back to the SD card** (`/meshcomod/tiles/{z}/{x}/{y}`). Tiles fetch,
  display, and are saved for offline use exactly as before — just on the card.
- The manual SD-tile mode (`/maps/osm/{z}/{x}/{y}.png`) is unchanged.

---

## 🐛 Fixes
- **Quick replies are editable again.** The editor's text fields weren't binding
  the keyboard (a stray composer callback left over from a refactor), so tapping a
  slot did nothing. They now open the keyboard like every other settings field —
  tap, type, **Save**.
- **Tile-cache SD mount is reliable.** The SD fallback now reuses the card that's
  already mounted for data storage instead of forcing a disruptive remount that
  could fail mid-boot — which is what kept the map stuck on the storage error
  under Launcher. The map's storage notice is also clearer now (it distinguishes
  "SD present but didn't mount" from "no card").

---

## 🔎 Known issues
- **Arabic keyboard is experimental** — RTL + letter-shaping render, but it wants
  more real-world testing.
- **Room servers**: the login-regression report from beta_7 is still open; beta_8
  keeps the serial diagnostics (`[ROOM] login send` / `[ROOM] login resp`). If you
  hit it, capture the device serial while joining a room and send it over.

---

## 📦 Flash

| Board | Merged — full image (USB install @ `0x0`) | App only (OTA / Launcher @ `0x10000`) |
|-------|-------------------------------------------|----------------------------------------|
| **LilyGo T-Deck / T-Deck Plus** | `LilyGo_TDeck_companion_radio_touch-merged.bin` | `LilyGo_TDeck_companion_radio_touch.bin` |
| **Heltec V4 TFT** | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` | `heltec_v4_tft_companion_radio_usb_tcp_touch.bin` |

- **From beta_7** → use the **app-only** bin (OTA or Launcher install).
- **From beta_6 or earlier** → do the one-time **merged** USB install first.

Status: **beta**.
