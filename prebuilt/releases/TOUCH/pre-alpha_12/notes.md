# Meshcomod TOUCH — pre-alpha_12

Thirteenth pre-alpha drop of the Meshcomod *touch* UI. On top of
[pre-alpha_11](../pre-alpha_11/notes.md): **over-the-air updates**, a big map
overhaul, emoji support, a control-center revamp, and battery fixes.

Targets in this drop:
- `heltec_v4_tft_companion_radio_usb_tcp_touch` — Heltec WiFi LoRa 32 V4 (TFT + cap touch)
- `LilyGo_TDeck_companion_radio_touch` — LilyGo T-Deck / T-Deck Plus (TFT + cap touch + trackball + QWERTY keyboard, optional GPS)

## ⚠️ One-time: flash the MERGED image to enable OTA

**This release changes the flash partition layout** (two equal OTA app slots so
the firmware can update itself over the air). A partition change **cannot** be
applied over the air, so for **this** update you must flash the **merged image
once**:

- Flash **`<board>-merged.bin` at offset `0x0`** (the web flasher's default /
  "full" option does this).
- After this one merged flash, **future updates are over-the-air** — no cable
  needed: **Settings → About → "Install update over Wi-Fi"**.

Your data is safe: the `tiles` and `spiffs` partitions keep their offsets, so
contacts, channels, chat history and Wi-Fi credentials survive the migration.
(Flashing the app-only `.bin` at `0x10000` over the OLD layout would mismatch
the new slot geometry — use the merged image for this release.)

## What's new since pre-alpha_11

**Over-the-air updates 🎉**
- Repartitioned to two equal **3.875 MB** OTA app slots (the old 1.5 MB backup
  slot couldn't hold the image). Firmware uses ~65 % of a slot.
- **Settings → About → "Install update over Wi-Fi"** downloads the latest
  release through the meshcomod proxy (plain HTTP — the device can't do
  on-device HTTPS) and reboots into it. Shows live progress and inline errors.

**Map — immersive full-screen**
- The map now fills the **whole screen**: the status bar and bottom menu go
  transparent (with black, map-legible text) and the tiles render behind them.
- **"© OpenStreetMap"** attribution moved into the top bar; coordinates sit in
  the bottom-left corner, marker/download status in the bottom-right.
- New **contacts-on-map picker** (☰ button): lists every contact that has shared
  GPS coords with **distance** and **time-heard**, **sortable** by name /
  distance / heard; tap one to recenter the map on it.
- Map options popup (⚙): link-lines toggle, **reload tiles in view** (repair a
  corrupt tile), and an about/credits sheet.

**Emoji + special characters**
- Names and messages now render **accents** (é ñ ü ç …), **smart quotes /
  dashes / currency / math** symbols, and **~130 monochrome emoji** instead of
  a `*` placeholder.
- An **emoji/symbol picker** (smiley button in the chat composer) inserts them;
  on the T-Deck the **trackball drives a grid selector** (roll to highlight,
  click to insert).

**Control center (tap the top bar)**
- **CPU + RAM% + PSRAM% + IP** on one line.
- Thinner brightness slider; tidier layout.
- A **power button** (top-right) opens a **Power off / Reboot** menu. Power off
  is a deep sleep — click the trackball to wake.

**Battery (T-Deck)**
- Calibrated voltage reading (a full battery used to read ~53 %).
- **Charging detection** — shows a charge icon + "CHG" while on USB.
- Smoothed % so it no longer jitters ±1 every second.

## Flash

| Board | This release (MERGED, `0x0`) — REQUIRED | Future OTA |
|-------|------------------------------------------|-----------|
| Heltec V4 TFT | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` | Settings → About |
| LilyGo T-Deck | `LilyGo_TDeck_companion_radio_touch-merged.bin` | Settings → About |

For this drop, use the **merged** image (it carries the new partition table).
The app-only `.bin` files are published for the OTA flow / future app-only
updates, not for this partition-changing flash.

T-Deck note: if auto-reset during flashing is flaky, hold the trackball (BOOT)
while tapping reset to enter download mode. A charge-only USB cable shows up as
"no serial data" — use a data cable.

## Known issues

- DMs from a peer whose advert hasn't been received yet cannot decrypt
  (MeshCore protocol-level). Auto-add for chat-type adverts is on.
- Camera-side QR *scanner* still not shipped — Share-my-contact is one-way.
- Map tile / elevation fetching depends on the meshcomod proxy.
- Multi-byte routing needs MeshCore ≥ v1.14 on every repeater in the path.
- Live Wi-Fi scanning requires Wi-Fi already on (Bluetooth and Wi-Fi can't share
  the radio heap simultaneously).
- All emoji render monochrome (the display path can't do colour emoji).
