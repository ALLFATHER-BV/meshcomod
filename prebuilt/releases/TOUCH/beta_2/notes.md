# TOUCH beta_2

Fixes the recovery being lost after installing a non-meshcomod firmware.

## ⚠️ Flash the MERGED bin + ERASE — everyone, including beta_1
beta_2 changes the **bootloader and partition table**, so it can't be applied
over the air. Do a full chip **erase**, then flash the `-merged.bin` at 0x0 over
USB. This applies to alpha users AND beta_1 users.

**T-Deck:** Export your settings before erasing, Import them back after.

After beta_2, meshcomod -> meshcomod `beta_N` updates are over-the-air again.

## What's fixed
- **Recovery survives installing other firmwares (e.g. Meshtastic).** In beta_1,
  using the recovery's "Add firmware" for a non-meshcomod build lost the recovery
  — the device booted straight into that firmware forever. beta_2 moves
  recovery-first into a small **custom bootloader** that boots the recovery by
  default and a firmware slot only as a one-shot, so the recovery always comes
  back no matter what is installed. A/B over-the-air OTA still works (the
  bootloader honors the OTA slot selection).

## Flash (per board)
| Board | Merged (ERASE + flash @0x0) | App-only (OTA) |
|---|---|---|
| LilyGo T-Deck | `LilyGo_TDeck_companion_radio_touch-merged.bin` | `LilyGo_TDeck_companion_radio_touch.bin` |
| Heltec V4 TFT | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` | `heltec_v4_tft_companion_radio_usb_tcp_touch.bin` |

The T-Deck merged image bundles the custom bootloader + recovery (factory) +
meshcomod (ota_0).

## Known issues
- Outgoing message timestamps can differ from the status-bar clock - fix pending.
- V4 has no on-screen charging indicator (ADC limitation).
