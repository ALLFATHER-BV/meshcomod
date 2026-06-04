# Meshcomod TOUCH — pre-alpha_13

Fourteenth pre-alpha drop. Mostly a reliability + polish pass on top of
[pre-alpha_12](../pre-alpha_12/notes.md): **OTA actually works now**, plus a
batch of Heltec V4 fixes.

Targets in this drop:
- `heltec_v4_tft_companion_radio_usb_tcp_touch` — Heltec WiFi LoRa 32 V4 (TFT + cap touch)
- `LilyGo_TDeck_companion_radio_touch` — LilyGo T-Deck / T-Deck Plus

## Updating

- **From pre-alpha_12:** update **over the air** — Settings → About → "Install
  update over Wi-Fi". (pre-alpha_12 and 13 share the same partition layout.)
- **From pre-alpha_11 or earlier:** flash the **merged** image once at `0x0`
  (the partition layout changed in 12 to enable OTA — a partition change can't
  be applied over the air). After that, future updates are over the air.

Your data (contacts, channels, chat history, Wi-Fi creds) survives either path.

## What's new since pre-alpha_12

**OTA now works end-to-end** (pre-alpha_12 shipped it but it didn't complete):
- Fixed an out-of-memory crash: the updater no longer spins up a TLS/HTTPS
  client for a plain-HTTP download (that reserved ~30 KB of scarce RAM and
  rebooted the device mid-start).
- Points the download at a host that serves the firmware over plain HTTP
  directly (the previous one redirected to HTTPS, which the device can't
  follow — it failed with "HTTP -1").
- The **"Install update" button greys out** when you're already on the latest
  version, and stays active while an update is available.

**Heltec V4 fixes**
- **Battery %**: reverted to the V4's direct voltage reading (a smoothing filter
  meant for the T-Deck was averaging it down to ~64 %). The V4 has no
  USB-charge-detect hardware, so it shows the real measured cell %, which rises
  as it charges.
- **Control center (top-bar dropdown) in portrait**: laid out properly for the
  narrow screen — the header stacks, the toggle chips fit, and the info line no
  longer overlaps them.
- **Brightness slider** added to the V4 control center (was T-Deck only).
- Small fixes: the "© OpenStreetMap" credit no longer overlaps the Wi-Fi icon on
  the map, and the Settings sub-tab labels no longer touch each other.

## Flash

| Board | From pre-alpha_11 or earlier (MERGED, `0x0`) | From pre-alpha_12 |
|-------|----------------------------------------------|-------------------|
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
  "charging" state).
