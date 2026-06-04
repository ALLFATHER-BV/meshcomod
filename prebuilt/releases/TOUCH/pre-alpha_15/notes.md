# Meshcomod TOUCH — pre-alpha_15

Sixteenth pre-alpha drop. Headline: **room-server support** (join + chat),
a **GPS serial-baud setting** (fixes T-Deck Plus vs v1.0 GPS), **map "zoom
packs"** that cache each place at two zoom levels for offline use, and a
**full route trace** in the message Info popup. On top of
[pre-alpha_14](../pre-alpha_14/notes.md).

Targets in this drop:
- `heltec_v4_tft_companion_radio_usb_tcp_touch` — Heltec WiFi LoRa 32 V4 (TFT + cap touch)
- `LilyGo_TDeck_companion_radio_touch` — LilyGo T-Deck / T-Deck Plus

## Updating

- **From pre-alpha_12, 13 or 14:** update **over the air** — Settings → About →
  "Install update over Wi-Fi". (Same partition layout.)
- **From pre-alpha_11 or earlier:** flash the **merged** image once at `0x0`.

Your data (contacts, channels, chat history, Wi-Fi creds) survives either path.

## What's new since pre-alpha_14

**Room servers — Join**
- Room-server contacts (the purple **SRV** ones) now have a **Join** action.
  Tap the contact → **Join** → enter the room password (blank = guest). On
  success you drop straight into the **room chat**: the server syncs the room's
  recent history and pushes new messages, and your sends post to the room.

**GPS serial baud**
- Set the GPS UART speed to match your hardware — the **T-Deck Plus** needs
  **38400**, the older **T-Deck v1.0** needs **9600**.
- Two ways: **Settings → Device → "GPS serial baud"** picker, or the CLI
  command **`set gps.baud 38400`** (also 9600 / 19200 / 57600 / 115200).
- Saved to the device and applied on the next **reboot**; survives reflashing.

**Map "zoom packs"**
- As you browse the map online, each location is now cached at **two zoom
  levels — a wide overview and a close detail** — in the background via the
  normal tile downloader, so places you've visited stay usable offline at both
  scales. The zoom buttons jump to the nearest cached level.

**Full route trace**
- Long-press a DM → **Info** → **Trace route** runs a multi-hop trace along the
  path to that contact and lists **each hop's SNR** (not just the first hop like
  the old repeater "Trace SNR"). DM threads only.

**UI fix**
- Settings dropdowns (region preset, path-hash, GPS baud) no longer slide
  off-screen behind the top status bar when they open near the lower half of
  the page.

## Flash

| Board | From pre-alpha_11 or earlier (MERGED, `0x0`) | From pre-alpha_12 / 13 / 14 |
|-------|----------------------------------------------|-----------------------------|
| Heltec V4 TFT | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` | OTA (Settings → About) |
| LilyGo T-Deck | `LilyGo_TDeck_companion_radio_touch-merged.bin` | OTA (Settings → About) |

T-Deck note: if auto-reset during flashing is flaky, hold the trackball (BOOT)
while tapping reset to enter download mode. A charge-only USB cable shows up as
"no serial data" — use a data cable.

## Known issues

- DMs from a peer whose advert hasn't been received yet cannot decrypt
  (MeshCore protocol-level). Auto-add for chat-type adverts is on.
- Room "Range test" in the action sheet would post a visible line into the room
  — use it only on direct/repeater contacts.
- Route trace needs a known path with forwarding repeaters in between; a
  flood-routed contact falls back to a single-hop trace.
- Camera-side QR *scanner* still not shipped — Share-my-contact is one-way.
- Map tile / elevation fetching depends on the meshcomod proxy.
- Multi-byte routing needs MeshCore ≥ v1.14 on every repeater in the path.
- Live Wi-Fi scanning requires Wi-Fi already on (Bluetooth and Wi-Fi can't share
  the radio heap simultaneously).
- All emoji render monochrome (the display path can't do colour emoji).
- The V4 has no charge-detect hardware, so the battery shows measured cell % (no
  "charging" state) and no sound.
