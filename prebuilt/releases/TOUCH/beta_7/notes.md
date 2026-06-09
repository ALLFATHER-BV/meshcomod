# Meshcomod Touch — beta_7

The biggest touch drop yet: six keyboard languages, a simpler & **Launcher-friendly**
boot/storage model, a much larger contact limit, optional SD-card storage,
standard PNG map tiles, and a batch of persistence fixes. Built on MeshCore 1.16.0.

---

## ⚠️ T-Deck: one-time full reflash required — read this first

beta_7 **removes the on-device recovery firmware** and moves the T-Deck to a
standard dual-OTA partition layout. This is deliberate groundwork for
[Launcher](https://github.com/bmorcelli/Launcher) compatibility (issue
[#19](https://github.com/ALLFATHER-BV/meshcomod/issues/19)) and drops a large
amount of custom-bootloader complexity.

Because the partition offsets changed, **you cannot OTA from beta_6 → beta_7.**
Flash the **merged image once over USB** (the web flasher does exactly this):

- It's a **clean install** — the device resets, so you'll run first-run setup
  again and your node comes up with a **new identity** (re-add it on peers).
- **After beta_7 is installed, normal over-the-air / phone-app updates work
  again.** This is the *last* full reflash; future betas are OTA. There's also no
  more boot countdown — it boots straight into meshcomod.
- **Heltec V4**: same merged-image flash, but the V4 never carried the recovery,
  so there's no behavioural change beyond the new features below.

---

## ✨ New

### Keyboards — six languages
- **Bulgarian, Russian, Ukrainian, Serbian, Greek**, and **Arabic** (experimental,
  right-to-left with letter-shaping). Huge thanks to **@kkazakov** for the original
  Bulgarian / Cyrillic work (PR [#15](https://github.com/ALLFATHER-BV/meshcomod/pull/15)).
- **Multi-select**: *Settings → Device → Secondary keyboards* — turn on any of them,
  then **double-tap SPACE** to cycle English → each enabled layout → back. A
  status-bar badge shows the active layout. On-screen and physical (T-Deck) keyboards
  both switch.

### Contact limit: 2000 (was 350)
- The contact table already lives in PSRAM, so the cap is now **2000** with no
  meaningful cost for users who have fewer.

### Optional SD-card storage
- *Settings → Device → Store data on SD (reboot)* keeps your identity, prefs,
  contacts and channels on the microSD card under **`/meshcomod/`** instead of
  internal flash — handy under Launcher (no SPIFFS partition) or just to keep
  everything on a card. Internal and SD are independent "worlds" — switching never
  wipes the other — and it falls back to internal automatically if the card can't
  be used.

### PNG map tiles — Meshtastic / MeshCore standard
- The SD map now reads standard **`/maps/osm/{z}/{x}/{y}.png`** tile trees,
  decoded directly via lodepng (bypassing the board's broken LVGL PNG path), so a
  normal OSM / Meshtastic tile pack just works. Legacy `/tiles/{z}/{x}/{y}.jpg`
  packs still load too.

---

## 🐛 Fixes
- **Auto-add contact settings now stick across reboots.** They auto-save the moment
  you toggle them (no separate Save press), and two underlying bugs are fixed: a
  boot-time block that reset them every start, and a `DataStore` save/load offset
  mismatch that also mis-loaded `gps_interval`, RX-boost and scope fields.

---

## 🔎 Known issues
- **Arabic is experimental** — RTL + shaping are enabled and render via a direct
  font path, but it wants more real-world testing.
- **Room servers**: a login regression was reported. The code paths check out end
  to end, so beta_7 ships **serial diagnostics** (`[ROOM] login send` / `[ROOM]
  login resp`) — if you hit it, capture the device serial while joining a room and
  send it over so it can be pinpointed and fixed precisely.

---

## 📦 Flash

| Board | Merged — full image (USB install @ `0x0`) | App only (OTA / Launcher @ `0x10000`) |
|-------|-------------------------------------------|----------------------------------------|
| **LilyGo T-Deck / T-Deck Plus** | `LilyGo_TDeck_companion_radio_touch-merged.bin` | `LilyGo_TDeck_companion_radio_touch.bin` |
| **Heltec V4 TFT** | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` | `heltec_v4_tft_companion_radio_usb_tcp_touch.bin` |

Use the **merged image** for the one-time USB install (web flasher). After that the
**app-only** bin is what OTA / Launcher install into the app slot.

Status: **beta**.
