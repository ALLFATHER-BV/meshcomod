# TOUCH beta_6

**Install firmware over Wi-Fi from the recovery, map tiles from the SD card — and the
T-Deck merged image bundles the recovery again.**

## What's new since beta_5
- **Recovery: install / update firmware over Wi-Fi (T-Deck).** Boot into recovery →
  **Install over Wi-Fi** → it connects (reusing the app's saved Wi-Fi, or scan + type),
  fetches a firmware catalog, downloads + **MD5-verifies** the image, and installs it —
  no SD card or computer needed. If a card is present it snapshots the outgoing firmware
  first and records each install as its own library entry. Catalog-driven, so more
  firmwares can be added later without reflashing the recovery.
- **Map tiles from the SD card (T-Deck).** Map → gear → **Tiles from SD card** reads
  `/tiles/<z>/<x>/<y>.jpg` straight off the card, fully offline (no tile server).
- **~16 KB more free internal RAM** (UI list buffers moved to PSRAM).
- Everything from beta_5 too: Wi-Fi-first onboarding, no-reboot Wi-Fi saves, live
  region/radio apply (no reboot after setup), and the T-Deck GPS fix.

## ⚠️ The T-Deck merged image includes the recovery again
The T-Deck `-merged.bin` is the **recovery-model image** (custom bootloader + recovery
@ factory + meshcomod @ ota_0), like **beta_2** — so a fresh flasher install at `0x0`
gives you the on-device recovery. (beta_3/4/5 merged shipped *without* it — a regression,
now fixed.)

## Updating
- **On beta_3 / 4 / 5?** The app update is **over-the-air** — Settings → **Install update
  over Wi-Fi**. To *also* get the recovery (or if you flashed a plain beta_3/4/5 image),
  flash the **`-merged.bin` at 0x0 with Erase** once. Export your settings first
  (Settings → Device → export), import them after.
- **Fresh install, or on alpha / beta_1 / beta_2?** Flash the **`-merged.bin` at 0x0 with
  Erase**.

## Flash (per board)
| Board | Merged (ERASE + flash @0x0) | App-only (OTA / slot) |
|---|---|---|
| LilyGo T-Deck | `LilyGo_TDeck_companion_radio_touch-merged.bin` (recovery model, ~3.7 MB) | `LilyGo_TDeck_companion_radio_touch.bin` |
| Heltec V4 TFT | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` | `heltec_v4_tft_companion_radio_usb_tcp_touch.bin` |

## Known issues
- Outgoing message timestamps can differ from the status-bar clock — fix pending.
- Heltec V4 TFT has no on-screen charging indicator (ADC limitation).
