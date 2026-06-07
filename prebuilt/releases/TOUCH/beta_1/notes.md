# TOUCH beta_1

First **BETA** of Meshcomod Touch — the channel moves from alpha to BETA.

## ⚠️ Migrating from an ALPHA build — read first
If you have NOT run a BETA build yet, you must **flash the MERGED bin and ERASE
the device** (full chip erase, then write the `-merged.bin` at 0x0, over USB).
The partition layout changed (dual-slot + on-device recovery), so an app-only /
over-the-air update from ALPHA will NOT work — it has to be a clean erase + merged
flash once.

**T-Deck:** before erasing you can **Export** your settings on your alpha build
and **Import** them back after flashing BETA.

Once you're on BETA, future `beta_N` updates install **over the air**
(Settings → Install update over Wi-Fi) — no more erasing.

## What's new since alpha
- **On-device recovery** baked in (T-Deck): boots to the recovery briefly, then
  launches meshcomod; hold the trackball during the countdown to stay in it.
- **No-SD in-firmware OTA** restored for BETA (dual A/B app slots).
- Boot splash now reads **TOUCH BETA**.
- **Battery calibration** (Settings → Device → Calibrate battery): fully charge,
  tap to set 100% — works for custom packs/builds. Steadier reading (~20 s
  refresh) and **instant charger connect/disconnect** detection (T-Deck).
- **@-mentions** insert the `@[name]` bracket form.

## Flash (per board)
| Board | Merged (ERASE + flash @0x0) | App-only (OTA) |
|---|---|---|
| LilyGo T-Deck | `LilyGo_TDeck_companion_radio_touch-merged.bin` | `LilyGo_TDeck_companion_radio_touch.bin` |
| Heltec V4 TFT | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` | `heltec_v4_tft_companion_radio_usb_tcp_touch.bin` |

The T-Deck merged image bundles the recovery (factory) + meshcomod (ota_0).

## Known issues
- Outgoing message timestamps can differ from the status-bar clock (separate clock
  sources) — fix pending.
- V4 has no on-screen charging indicator (ADC limitation); % + calibration work.
