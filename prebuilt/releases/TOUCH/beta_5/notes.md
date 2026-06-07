# TOUCH beta_5

**Wi-Fi-first onboarding, fewer reboots — and the T-Deck GPS gets a fix again.**

## What's new since beta_4
- **Wi-Fi on / Bluetooth off out of the box.** A freshly-flashed device now comes
  up on Wi-Fi (scannable) instead of BLE, so first-boot setup can scan for and
  join a network immediately. Switching to BLE is one tap in Settings → Bluetooth.
- **Saving Wi-Fi no longer reboots** when Wi-Fi is already on — it just reconnects
  with the new network. (Only an actual transport switch to/from Bluetooth reboots.)
- **Setup wizard Wi-Fi step:** the Scan button now works and the network picker
  pops up automatically with the nearby SSIDs — tap one to fill it in.
- **Finishing setup no longer reboots**, and changing region / radio settings now
  takes effect immediately (live radio apply) instead of on the next boot.
- **T-Deck GPS fix (regression).** Since the 1.16 rebase, GPS could fail to get a
  fix unless you toggled it off/on a few times. The on-board GPS now starts
  reliably and **resumes automatically on boot**.

## Updating
- **On beta_3 or beta_4?** Over-the-air: Settings → **Install update over Wi-Fi**
  (or flash the app-only `.bin` at the meshcomod slot). No erase needed —
  bootloader + partition table are unchanged since beta_2.
- **On alpha / beta_1 / beta_2, or a fresh install?** Flash the **`-merged.bin`
  at 0x0 with Erase device** checked. T-Deck: export your settings first
  (Settings → Device), import them after.

## Flash (per board)
| Board | Merged (ERASE + flash @0x0) | App-only (OTA / meshcomod slot) |
|---|---|---|
| LilyGo T-Deck | `LilyGo_TDeck_companion_radio_touch-merged.bin` | `LilyGo_TDeck_companion_radio_touch.bin` |
| Heltec V4 TFT | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` | `heltec_v4_tft_companion_radio_usb_tcp_touch.bin` |

## Known issues
- Outgoing message timestamps can differ from the status-bar clock — fix pending.
- Heltec V4 TFT has no on-screen charging indicator (ADC limitation).
