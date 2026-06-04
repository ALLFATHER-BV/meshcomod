# TOUCH pre-alpha_18

Built: 05-Jun-2026 · version `20260605_000523-5c17282e`

## What's new since pre-alpha_17

**Fixes the BLE ⇄ Wi-Fi switch + on-device Wi-Fi scanning.**

Previously, if you set the device up over Bluetooth (or skipped Wi-Fi in the
first-boot wizard), there was no way to get onto Wi-Fi without *typing* a network
name blind — the Bluetooth/Wi-Fi toggles appeared to do nothing and "Scan" found
no networks, because the radio only ever came up if credentials were already
saved.

Now:

- **Settings → Bluetooth**, toggle Bluetooth **off** → the device switches to
  Wi-Fi (reboots) and comes up ready to scan — no "save Wi-Fi creds first".
- **Settings → Wi-Fi → Scan** while on Bluetooth → offers a one-tap *"Switch to
  Wi-Fi and reboot so you can scan?"* → then scan, tap your network, enter the
  password, Save.
- **Control Center** Wi-Fi toggle does the same clean switch.
- A fresh device that skips Wi-Fi still uses Bluetooth, exactly as before.

While Bluetooth is the active transport you still can't scan (the radio can't be
up at the same time) — but switching to Wi-Fi is now one tap, no SSID typing.

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
