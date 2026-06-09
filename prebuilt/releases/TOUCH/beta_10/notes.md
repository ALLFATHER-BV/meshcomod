# Meshcomod Touch — beta_10

A small, **Heltec V4 TFT–focused** drop. **LilyGo T-Deck users can skip this one**
if they want — there's nothing T-Deck-specific in it (the T-Deck already had
everything here). Built on MeshCore 1.16.0.

---

## ⬆️ Updating from beta_9 — no reflash needed

Same partition layout — a normal **over-the-air / Launcher app update** (install
the app-only bin). No USB reflash, no identity reset. Coming from beta_6 or
earlier? Do the one-time **merged-image USB install** first.

The bins ship **no recovery firmware** (T-Deck merged = clean bootloader +
dual-OTA app).

---

## ✨ New — Heltec V4 TFT

### Keyboard languages are now usable on the V4
- The extra keyboard languages (Bulgarian, Russian, Ukrainian, Serbian, Greek,
  Arabic) could previously only be switched by **double-tapping the T-Deck's
  physical SPACE** — so the V4 (on-screen keyboard only) was stuck on English even
  with secondary layouts enabled.
- The on-screen keyboard now has a small **language key** showing the active
  layout's 2-letter code (`EN`, `RU`, `BG`, …). **Tap it to cycle** English → each
  enabled layout → back. Enable layouts in *Settings → Device → Secondary
  keyboards*; the key appears whenever at least one is on, sits beside the rotate
  arrows, and fades while you type. The active layout is remembered across reboots.

---

## 🐛 Fixes
- **Clearer OTA error.** A device still on the ancient lopsided partition table
  (a tiny 1.5 MB OTA slot) can't fit a ~2.6 MB update, and the old
  `ERR: image X > OTA Y` was opaque. It now reads *"too big for OTA slot —
  re-flash the merged image once over USB to enable OTA."* If you hit this, flash
  the **merged** image once: it installs the equal 3.875 MB dual-OTA slots and
  keeps your tiles + user data (same offsets).

---

## 📦 Flash

| Board | Merged — full image (USB install @ `0x0`) | App only (OTA / Launcher @ `0x10000`) |
|-------|-------------------------------------------|----------------------------------------|
| **LilyGo T-Deck / T-Deck Plus** | `meshcomod-tdeck-merged.bin` | `meshcomod-tdeck.bin` |
| **Heltec V4 TFT** | `meshcomod-heltec-v4-tft-merged.bin` | `meshcomod-heltec-v4-tft.bin` |

- **From beta_9** → use the **app-only** bin (OTA or Launcher install).
- **From beta_6 or earlier** → do the one-time **merged** USB install first.

> The env-named `LilyGo_TDeck_companion_radio_touch*.bin` /
> `heltec_v4_tft_companion_radio_usb_tcp_touch*.bin` filenames are still published
> as OTA aliases so devices on an earlier beta keep self-updating.

Status: **beta**.
