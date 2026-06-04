# TOUCH pre-alpha_17

Built: 04-Jun-2026 · version `20260604_232543-d0ce2958`

## What's new since pre-alpha_16

- **Chat history now survives firmware updates.** The on-disk history format is
  self-describing (v5): message-format changes no longer wipe your chats.
  Upgrading from pre-alpha_16 keeps your existing messages, and future updates
  won't lose them either.
- **Map options popup** now has a close (X) button (tap-outside still works).
- **Settings overhaul:** no top status bar on the Settings page, redundant
  sub-tab titles removed, "Prof" → "Profile", and your identity + Share-QR
  moved to the bottom of the Profile tab.
- **Chat screen:** the in-chat header bar is gone (more room for messages); a
  HOME button returns you to the list, and the channel name + unread badge now
  live in the top status bar. Slimmer composer and bottom nav bar.
- **Wi-Fi status** on the Network tab no longer sticks on "loading", and the
  IP / signal / WebSocket line no longer overlaps.
- **Notification chime** plays off-thread, so the new-message toast no longer
  briefly freezes the UI.

## Flash

| Board | First time / before pre-alpha_12 | Already on pre-alpha_12+ |
|-------|----------------------------------|--------------------------|
| Heltec V4 TFT | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` @ `0x0` | Install update over Wi-Fi (Settings), or `…_touch.bin` @ `0x10000` |
| LilyGo T-Deck | `LilyGo_TDeck_companion_radio_touch-merged.bin` @ `0x0` | Install update over Wi-Fi (Settings), or `…_touch.bin` @ `0x10000` |

Flashing the **merged** image at `0x0` is only needed once (it lays down the
OTA partition table) and resets saved Wi-Fi credentials. After that, use the
in-app "Install update over Wi-Fi".

## Status

Pre-alpha. Both boards build from the open-source `ui-touch/` tree on `main`.
