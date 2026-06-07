# TOUCH beta_4

**Fixes the LilyGo T-Deck ~2-minute slow boot introduced in beta_3.**

beta_3 (the MeshCore 1.16.0 integration) regressed boot time on the T-Deck: it
could sit on the meshcomod splash for ~2 minutes before the UI appeared. Root
cause: 1.16's sensor manager scans all 128 I2C addresses at boot, and on the
T-Deck that's the **main bus shared with the keyboard + touch controller**, which
clock-stretches every probe → a multi-minute stall in `sensors.begin()`. (Two
sensor entries were also guarded with `#ifdef` instead of `#if`, so the T-Deck's
`ENV_INCLUDE_*=0` still pulled them in and forced the scan.) beta_4 skips the scan
on boards with no I2C sensors. **Boot is back to ~12 s.**

Heltec V4 TFT wasn't affected (different I2C layout) but is rebuilt from the same
source for parity.

## Updating
- **On beta_3?** This is an **over-the-air** update — Settings → **Install update
  over Wi-Fi** (or flash the app-only `.bin` at the meshcomod slot). No erase
  needed; bootloader + partition table are unchanged since beta_2.
- **On alpha / beta_1 / beta_2, or a fresh install?** Flash the **`-merged.bin` at
  0x0 with Erase device** checked. T-Deck: export your settings first, import after.

## Flash (per board)
| Board | Merged (ERASE + flash @0x0) | App-only (OTA / @0x10000) |
|---|---|---|
| LilyGo T-Deck | `LilyGo_TDeck_companion_radio_touch-merged.bin` | `LilyGo_TDeck_companion_radio_touch.bin` |
| Heltec V4 TFT | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` | `heltec_v4_tft_companion_radio_usb_tcp_touch.bin` |

The T-Deck merged image bundles the custom bootloader + recovery (factory) +
meshcomod (ota_0).

## Known issues
- Outgoing message timestamps can differ from the status-bar clock — fix pending.
- V4 has no on-screen charging indicator (ADC limitation).
