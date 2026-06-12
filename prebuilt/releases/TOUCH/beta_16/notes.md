# Meshcomod Touch — beta_16

The localization + polish drop: **the whole UI now speaks 11 languages**, **full-colour
emoji**, a proper **Backups** screen (export / restore / delete / factory-reset), and a
stack of settings polish — for both boards (Heltec V4 TFT and LilyGo T-Deck / T-Deck Plus).

> ⚠️ **Over-the-air (Wi-Fi) self-update is still paused.** The "update available"
> badge still works; the button points you at the manual flasher.

---

## ⬆️ Updating from beta_8–15 — app-only, nothing resets

Same partition layout, so this is a **plain app-only update** — no full USB reflash,
**no identity / contacts / channels / message reset**:

- **T-Deck under Launcher:** reinstall the app-only bin (`meshcomod-tdeck.bin`).
- **USB / web flasher:** **flasher.meshcomod.com** → install the app-only image.

Fresh device, or coming from **beta_6 or earlier**? Do the one-time merged-image
USB install (`*-merged.bin` at `0x0`, **Erase** first).

---

## ✨ New

- **UI languages (11).** The whole interface can now run in **English, Nederlands,
  Deutsch, Français, Español, Italiano, Русский, Українська, Български, Српски,
  Ελληνικά**. Pick it in **Settings → Language** (reboots to apply). Anything not yet
  translated falls back to English — never blank.
- **Full-colour emoji.** ~250 colour emoji render inline in chats and text boxes
  (and the existing emoji/symbol picker is now in colour). Baseline-aligned so they
  sit on the line with your text.
- **Backups (Settings → Backups).** One place to **Export** a backup (timestamped,
  saved to the SD card if one's in, else internal flash), see **every** backup
  listed, **delete** individual ones, and a **Factory reset** that wipes the device
  back to first-boot (keeps your map tiles).
- **Person icon** for the Contacts tab in the bottom bar.

## 🛠️ Settings & polish

- **Settings restructure polish:** category cards and long rows now **wrap** so
  translated text fits; clearer that the list scrolls.
- **Toggles follow your theme colour** instead of always being blue
  (Settings → Display → Theme colour).
- **Disable accent pop-ups** while typing — Settings → Keyboard.
- **Swipe left→right to go back** in settings sub-pages and open chats/channels
  (iPhone-style edge-back).
- **Delete** button for saved Wi-Fi quick-connect profiles.
- **Battery mV removed** from the home screen (cleaner; the % + icon stay).
- **`.PNG` map tiles** on the SD card are recognised too (not only `.png`).

## 🐞 Fixes

- **Non-Latin text no longer shows boxes (□)** on labels that used the default font —
  the full glyph set (Cyrillic / Greek / Arabic) is now the inherited default.
- **Swiping to the Map** no longer lands on a black map (it now recenters/loads the
  same as tapping the tab).

---

*Both boards: `*-merged.bin` = full image (flash at `0x0`, Erase first). `*.bin` =
app-only (OTA / Launcher / flasher app-update slot).*
