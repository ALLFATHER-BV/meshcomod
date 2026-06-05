# TOUCH pre-alpha_20

Built: 05-Jun-2026 · version `20260605_200415-5c17282e`

## What's new since pre-alpha_19

**Fixes a random reset (task-watchdog timeout) during internal-flash writes.**

A coredump from a spontaneous reboot showed a **task watchdog timeout**: the
`ipc0` task aborted while the internal SPIFFS filesystem was doing a
**garbage-collection** pass mid-write. SPIFFS GC is a multi-second flash burst
that disables the CPU cache and can starve the idle task long enough to trip the
5 s watchdog — so a routine write (chat-history save, settings export/import)
could occasionally reset the device. It looked partition-related in the trace
but was not.

Now the heavier internal-flash writes — **chat-history save**, **settings
export**, and **settings import** — suspend the per-core idle watchdogs for the
duration (ref-counted, the same guard the file-manager format/paste paths
already use), so a bounded-but-slow GC pass can complete instead of resetting.

No functional/UI changes from pre-alpha_19 otherwise.

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
