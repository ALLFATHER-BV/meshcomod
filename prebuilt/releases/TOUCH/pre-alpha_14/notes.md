# Meshcomod TOUCH — pre-alpha_14

Fifteenth pre-alpha drop. Headline: a **fix for the map reboot** that could hit
devices after the partition change in pre-alpha_12, plus **notification sound on
the T-Deck**. On top of [pre-alpha_13](../pre-alpha_13/notes.md).

Targets in this drop:
- `heltec_v4_tft_companion_radio_usb_tcp_touch` — Heltec WiFi LoRa 32 V4 (TFT + cap touch)
- `LilyGo_TDeck_companion_radio_touch` — LilyGo T-Deck / T-Deck Plus

## Updating

- **From pre-alpha_12 or 13:** update **over the air** — Settings → About →
  "Install update over Wi-Fi". (Same partition layout.)
- **From pre-alpha_11 or earlier:** flash the **merged** image once at `0x0`.

Your data (contacts, channels, chat history, Wi-Fi creds) survives either path.

## What's new since pre-alpha_13

**Map reboot fixed (recommended update)**
- Panning/zooming the map into a fresh area could reboot the device while tiles
  downloaded. Root cause (found via on-device coredump): the partition-table
  change in pre-alpha_12 could leave the map-tile filesystem in a corrupt state
  that still "mounted" but crashed the moment a new tile folder was created
  (a divide-by-zero deep inside the filesystem). The firmware now detects this
  and automatically reformats the tile cache on boot — it self-heals. (The tile
  cache is just re-downloadable map data, so nothing important is lost.)

**Notification sound (T-Deck)**
- The T-Deck now plays a short chime through its speaker when a message arrives.
- Toggle it from the control center (the 🔊 Sound chip) or Settings → Device.
  Off by default; turning it on plays a confirmation chime.
- (The Heltec V4 has no speaker hardware, so it has no sound option — the old
  dead "buzzer" toggle was removed there.)

## Flash

| Board | From pre-alpha_11 or earlier (MERGED, `0x0`) | From pre-alpha_12 / 13 |
|-------|----------------------------------------------|------------------------|
| Heltec V4 TFT | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` | OTA (Settings → About) |
| LilyGo T-Deck | `LilyGo_TDeck_companion_radio_touch-merged.bin` | OTA (Settings → About) |

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
- The V4 has no charge-detect hardware, so the battery shows measured cell % (no
  "charging" state) and no sound.
