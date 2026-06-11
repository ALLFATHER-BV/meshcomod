# Meshcomod Touch — beta_15

The big one: **Wi-Fi and Bluetooth at the same time.** Plus a Discord-style unread
marker, an ignore/block list, cross-region login fixes, and a pile of polish — for
both boards (Heltec V4 TFT and LilyGo T-Deck / T-Deck Plus).

> ⚠️ **Over-the-air (Wi-Fi) self-update is still paused.** The "update available"
> badge still works; the button points you at the manual flasher.

---

## ⚠️ Read this before updating

Two one-time resets on this update — both expected, nothing's broken:

1. **Chat history is cleared once.** Messages can now be up to **160 characters**
   (was 96), which changes the chat-history storage format, so the old history is
   reset on first boot. **Your contacts and channels are kept** — only past
   messages clear.
2. **Re-pair Bluetooth once.** The BLE stack changed (Bluedroid → NimBLE), so
   existing pairings are invalidated. On your phone: *Forget* the old MeshCore
   device in Bluetooth settings, then pair again (same 6-digit PIN flow).

---

## ⬆️ Updating from beta_8–14 — app-only, no identity reset

Same partition layout, so this is a **plain app-only update** — no full USB reflash,
no identity/contacts reset:

- **T-Deck under Launcher:** reinstall the app-only bin (`meshcomod-tdeck.bin`).
- **USB / web flasher:** **flasher.meshcomod.com** → install the app-only image.

Fresh device, or coming from **beta_6 or earlier**? Do the one-time merged-image
USB install (`*-merged.bin` at `0x0`, **Erase** first).

---

## ✨ New

- **Wi-Fi + Bluetooth coexistence.** The companion BLE stack moved from Bluedroid
  to **NimBLE**, which is light enough to run alongside Wi-Fi. Wi-Fi and Bluetooth
  now work **at the same time**, and the Wi-Fi / BT toggles in the control center
  are **live** — no more reboot-to-switch. Turning Bluetooth off now also **stays
  off across a reboot**.
- **Discord-style unread marker.** Open a chat or channel with unread messages and
  a **"New"** divider line marks where you left off — the view opens there instead
  of jumping to the bottom. A floating **↓** button bottom-right jumps you to the
  latest message; it clears when you leave the chat.
- **Ignore / block list.** Long-press a message → **Block** to mute that sender —
  their messages stop showing and stop notifying (DMs and channels). Manage the
  list from the **⚙ gear** that now appears in any open chat → **Blocked users**
  → *Unblock*. The list is saved (up to 32).
- **Heltec V4 sound (expansion-kit buzzer).** The V4 now drives the optional
  expansion-kit piezo buzzer (GPIO6) for the notification chime, with a **Sound**
  toggle — matching the T-Deck speaker. (Only makes sound on units that have the
  snap-in buzzer fitted.)

---

## 🐞 Fixed

- **Cross-region login.** Logging in to a repeater or room server that's under a
  different region scope now works — login / DM / status requests are no longer
  locked to your home region's flood scope. (Public channel messages stay scoped.)
- **Chat history now persists reliably.** Fixed a storage-resolution bug where the
  chat history could fail to save (notably on the V4's internal storage), so it
  was effectively memory-only and lost on reboot. History now sticks across
  reboots on both boards.
- **Longer messages.** Message text limit raised 96 → 160 chars; long messages
  wrap fully instead of being cut off at the third line.
- **Lat/long save.** Coordinates entered with a comma decimal (e.g. `50,8466`) or
  a trailing space now save correctly.
- **Auto-add is foolproof.** The four "Auto-add" type switches (chat / repeater /
  room / sensor) are now the single source of truth — *off* genuinely means off.
  Removed the hidden master toggle that could let a type leak through.
- **Delete contact sticks.** Deleting a contact on the device now persists — it no
  longer reappears after a reboot.
- **Status polish.** The status-bar connection icon shows **both** Wi-Fi and BT
  when both are on; the home screen shows the IP (or *Offline*) and the
  control-center info line refreshes the IP live.

---

## Flash

| Board | Fresh / from ≤ beta_6 (USB @ `0x0`, **Erase** first) | Update from beta_8+ (app-only @ `0x10000`) |
|-------|------------------------------------------------------|--------------------------------------------|
| **Heltec V4 TFT** | `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin` | `heltec_v4_tft_companion_radio_usb_tcp_touch.bin` |
| **LilyGo T-Deck** | `LilyGo_TDeck_companion_radio_touch-merged.bin` | `LilyGo_TDeck_companion_radio_touch.bin` |

Easiest path is the web flasher at **flasher.meshcomod.com** (rolling
`meshcomod-tdeck` / `meshcomod-heltec-v4-tft` names), or grab the versioned bins
from `prebuilt/releases/TOUCH/beta_15/`.

NVS-preserving USB flash (keeps Wi-Fi creds) writes the four parts individually
(`0x0` bootloader, `0x8000` partitions, `0xe000` boot_app0, `0x10000` firmware) —
the merged image at `0x0` is only for the deliberate fresh install (Erase + write).
