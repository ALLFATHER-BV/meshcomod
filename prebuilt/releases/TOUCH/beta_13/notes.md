# Meshcomod Touch — beta_13

A small **stability / cleanup** drop on top of beta_12, for both boards (Heltec V4
TFT and LilyGo T-Deck / T-Deck Plus). One focused fix — no feature or UI changes.

> ⚠️ **Over-the-air (Wi-Fi) self-update is still paused.** The "update available"
> badge still works; the button points you at the manual flasher.

---

## ⬆️ Updating from beta_8–12 — app-only, no identity reset

Same partition layout, so this is a **plain app-only update** — no USB reflash, no
identity/contacts reset:

- **T-Deck under Launcher:** reinstall the app-only bin (`meshcomod-tdeck.bin`).
- **USB / web flasher:** **flasher.meshcomod.com** → install the app-only image.

Fresh device, or coming from **beta_6 or earlier**? Do the one-time merged-image USB
install (`*-merged.bin` at `0x0`, **Erase** first).

---

## 🐞 Fixed

- **No more serial-console error spam.** The firmware logged a Preferences error
  (`[E] … nvs_get_str … NOT_FOUND`) every time it read an **empty Wi-Fi
  quick-connect slot** — which happens on every Network-tab open — and for several
  unset settings on a **fresh device**. On the touch boards the USB serial console
  is shared with the binary companion protocol, so this stray text was also being
  injected into the protocol stream. Reads now probe with `isKey()` first, so absent
  keys are silent: the console **and** the companion serial link stay clean.

---

## Flash

| Board | Fresh / from ≤ beta_6 (USB @ `0x0`, **Erase** first) | Update from beta_8+ (app-only @ `0x10000`) |
|-------|------------------------------------------------------|--------------------------------------------|
| **Heltec V4 TFT** | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` | `heltec_v4_tft_companion_radio_usb_tcp_touch.bin` |
| **LilyGo T-Deck** | `LilyGo_TDeck_companion_radio_touch-merged.bin` | `LilyGo_TDeck_companion_radio_touch.bin` |

Easiest path is the web flasher at **flasher.meshcomod.com** (rolling
`meshcomod-tdeck` / `meshcomod-heltec-v4-tft` names), or grab the versioned bins
from `prebuilt/releases/TOUCH/beta_13/`.

NVS-preserving USB flash (keeps Wi-Fi creds) writes the four parts individually
(`0x0` bootloader, `0x8000` partitions, `0xe000` boot_app0, `0x10000` firmware) —
the merged image at `0x0` is only for the deliberate fresh install (Erase + write).
