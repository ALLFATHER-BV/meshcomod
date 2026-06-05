# TOUCH pre-alpha_19

Built: 05-Jun-2026 · version `20260605_194109-5c17282e`

## What's new since pre-alpha_18

**Settings backup (export / import) + richer message routing info + more
reliable microSD.**

### Settings export / import
- **Settings → Profile → Export settings** writes a **MeshCore-app-compatible
  JSON backup** — node name, public/private key, radio settings, position,
  channels and contacts — to the SD card (if present) or internal flash. The
  file opens in the stock MeshCore app and web client.
- **Import settings** opens an on-device **file picker** (internal flash + SD)
  listing every `.json` backup; pick one to restore. Works with this firmware's
  own export *and* a stock-app export. It replaces identity, channels and
  contacts, then reboots so radio settings take effect.

### Message Info — full routing detail
- **Received** messages now show the **full route trace** (all hops, hex IDs),
  the path **hash size**, and the **scope** code.
- **Sent** flood messages show **repeats heard** — how many times the mesh was
  seen echoing your message — in the Info popup and as a small tag on the chat
  bubble.

### microSD reliability
- The mount routine now **falls back to slower SPI clocks** (4 MHz → 1 MHz →
  400 kHz) for cold / cheap cards that previously only mounted after a physical
  eject-and-reinsert. The import picker also actively mounts the card on open.
- The file manager and import picker now **hide OS-metadata cruft** on FAT cards
  (macOS `._*` AppleDouble sidecars, `.DS_Store`, `__MACOSX`, Windows
  `System Volume Information`). Toggle "Show system files" to reveal them.

## Flash

| Board | First time / before pre-alpha_12 | Already on pre-alpha_12+ |
|-------|----------------------------------|--------------------------|
| Heltec V4 TFT | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` @ `0x0` | Install update over Wi-Fi (Settings), or `…_touch.bin` @ `0x10000` |
| LilyGo T-Deck | `LilyGo_TDeck_companion_radio_touch-merged.bin` @ `0x0` | Install update over Wi-Fi (Settings), or `…_touch.bin` @ `0x10000` |

Flashing the **merged** image at `0x0` is only needed once (lays down the OTA
partition table) and resets saved Wi-Fi credentials. After that, use the in-app
"Install update over Wi-Fi".

## Status

Pre-alpha. Both boards build from the open-source `ui-touch/` tree on `main`.
