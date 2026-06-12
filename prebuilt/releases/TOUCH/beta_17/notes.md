# Meshcomod Touch — beta_17

The Launcher-fixes + map + telemetry drop — for both boards (Heltec V4 TFT and
LilyGo T-Deck / T-Deck Plus).

> ⚠️ **Over-the-air (Wi-Fi) self-update is still paused.** The "update available"
> badge still works; the button points you at the manual flasher.

---

## ⬆️ Updating from beta_8–16 — app-only, nothing resets

Same partition layout, so this is a **plain app-only update** — no full USB reflash,
**no identity / contacts / channels / message reset**:

- **T-Deck under Launcher:** reinstall the app-only bin (`meshcomod-tdeck.bin`).
- **USB / web flasher:** **flasher.meshcomod.com** → install the app-only image.

Fresh device, or coming from **beta_6 or earlier**? Do the one-time merged-image
USB install (`*-merged.bin` at `0x0`, **Erase** first).

---

## 🚀 Running under Launcher? This one's for you

The app-only bin on Launcher's partition table has no usable SPIFFS and a full NVS,
which used to mean settings never saved and, in some cases, a boot crash.

- **Settings now persist.** When NVS isn't usable, all preferences (UI language,
  theme, Wi-Fi credentials, Bluetooth on/off, favourites, ignore list, battery
  calibration, keyboard layouts, …) are stored on the **SD card** (`/meshcomod/*.kv`)
  instead of silently failing. The flood of `nvs_open` errors in the log is gone too.
- **No more boot crash.** A bounded BLE device-name fixes a `Stack smashing protect
  failure!` that could crash-loop a Launcher install at boot.

## 🗺️ Map

- **Zoom in & out the full depth of your tiles.** The zoom range was capped at z12–z16;
  it's now **z3–z19**, so the +/− buttons go as far as your offline pack (or Wi-Fi)
  actually has — stopping at the edge of your tiles instead of an arbitrary limit.
- **See a contact's position from telemetry** *(issue #27)*. If a contact doesn't flood
  position adverts but answers **Telemetry** with a GPS field, that location is now saved
  to the contact and they show up on the **Map** (a 📍 in the telemetry toast confirms it).
- Uppercase **`.PNG`** SD tiles are recognised (not only `.png`).

## ✨ Polish

- **Emoji ~15% larger**, and they sit on the text baseline.
- Chat composer **text is vertically centred** in the box (and no longer bobs as you type).
- The **chat / channel list keeps its scroll position** — a background refresh no longer
  snaps you back to the top while you're scrolling.

---

*Both boards: `*-merged.bin` = full image (flash at `0x0`, Erase first). `*.bin` =
app-only (OTA / Launcher / flasher app-update slot).*
