# TOUCH beta_3

**Now based on MeshCore 1.16.0.** beta_3 brings the meshcomod touch firmware up to
the upstream MeshCore 1.16.0 release — newer mesh protocol, radio improvements, and
new features — while keeping all the meshcomod extras (touch UI, multi-transport
USB/BLE/TCP, message sync, Wi-Fi OTA, recovery, battery calibration). Every board
(V3 / V4 / V4-TFT / T-Deck / Xiao S3 / Wireless Paper) was rebuilt on the 1.16.0 base.

## Updating
- **Already on beta_2?** This is an **over-the-air** update — open Settings and tap
  **Install update over Wi-Fi** (or flash the app-only `.bin` at 0x10000). No erase
  needed: the bootloader and partition table are unchanged since beta_2.
- **On alpha / beta_1, or a fresh install?** Flash the **`-merged.bin` at 0x0 with
  Erase device checked** (the partition layout changed at beta_2). T-Deck: export
  your settings first, import them back after.

## Highlights
- **MeshCore 1.16.0 mesh protocol** — extended ACK delivery receipts, un-scoped
  flood + default flood scope, raw-packet send, anonymous/non-contact requests.
  Interoperates with 1.16 nodes on the air.
- **Radio** — longer preamble at low spreading factors for better range; refactored
  radio driver; corrected EU repeater frequency (869.000 → 869.495 MHz).
- **Touch UX** — battery calibration (tap to set 100%), faster charger detection,
  steadier 20-second battery/voltage readout, `@[name]` mention insertion.

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
- The 1.16 delivery-receipt (✓✓) path is newly integrated — please report any
  message-delivery anomalies against 1.16 nodes.
