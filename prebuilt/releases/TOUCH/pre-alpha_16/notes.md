# Meshcomod TOUCH — pre-alpha_16

Seventeenth pre-alpha drop. **Stability fix**: resolves an on-screen-keyboard
crash that could reboot the device. Strongly recommended for everyone on
pre-alpha_15 (or earlier). On top of [pre-alpha_15](../pre-alpha_15/notes.md).

Targets in this drop:
- `heltec_v4_tft_companion_radio_usb_tcp_touch` — Heltec WiFi LoRa 32 V4 (TFT + cap touch)
- `LilyGo_TDeck_companion_radio_touch` — LilyGo T-Deck / T-Deck Plus

## Updating

- **From pre-alpha_12 … 15:** update **over the air** — Settings → About →
  "Install update over Wi-Fi". (Same partition layout.)
- **From pre-alpha_11 or earlier:** flash the **merged** image once at `0x0`.

Your data (contacts, channels, chat history, Wi-Fi creds) survives either path.

## What's new since pre-alpha_15

**Keyboard crash fixed (recommended update)**
- A use-after-free in the on-screen-keyboard "mirror" could reboot the device.
  When a text field's popup was torn down by a path that didn't dismiss the
  keyboard first — e.g. a hardware-key action or the screen-lock countdown
  firing while a field was focused — the keyboard kept a pointer to the freed
  field and crashed (`loopTask`, LoadProhibited) on the next keystroke or tap.
  Diagnosed from the on-device coredump. The keyboard binding is now cleared
  automatically the moment a field is destroyed, so it can never point at freed
  memory regardless of how the field is closed.

Everything from pre-alpha_15 (room-server Join, GPS serial baud, map zoom packs,
full route trace, settings dropdown clamp) carries forward unchanged.

## Flash

| Board | From pre-alpha_11 or earlier (MERGED, `0x0`) | From pre-alpha_12 … 15 |
|-------|----------------------------------------------|------------------------|
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
