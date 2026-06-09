# Meshcomod Touch — beta_9

A small fix-up on top of beta_8 for **settings import** (the MeshCore-app / device
JSON backup). Built on MeshCore 1.16.0.

---

## ⬆️ Updating from beta_8 — no reflash needed

Same partition layout as beta_7/beta_8, so this is a normal **over-the-air /
Launcher app update** — just install the **app-only** bin. No USB reflash, no
identity reset. Coming from beta_6 or earlier? Do the one-time **merged-image USB
install** first.

The bins ship **no recovery firmware** (the T-Deck merged image is a clean
bootloader + dual-OTA app).

---

## 🐛 Fixes — settings import

- **The import file picker now scans `/meshcomod` on the SD card, not just the
  card root.** SD-storage / Launcher installs keep their data under `/meshcomod`,
  so a backup dropped there (right next to your data) is now listed — previously
  only the card root was scanned, so it appeared as "only recognized from the root
  folder".
- **Import now shows a result before rebooting** — *"Imported N contacts, M
  channels"* — instead of a silent restart that could look like nothing happened.

---

## 📦 Flash

| Board | Merged — full image (USB install @ `0x0`) | App only (OTA / Launcher @ `0x10000`) |
|-------|-------------------------------------------|----------------------------------------|
| **LilyGo T-Deck / T-Deck Plus** | `meshcomod-tdeck-merged.bin` | `meshcomod-tdeck.bin` |
| **Heltec V4 TFT** | `meshcomod-heltec-v4-tft-merged.bin` | `meshcomod-heltec-v4-tft.bin` |

- **From beta_8** → use the **app-only** bin (OTA or Launcher install).
- **From beta_6 or earlier** → do the one-time **merged** USB install first.

> The previous `LilyGo_TDeck_companion_radio_touch*.bin` /
> `heltec_v4_tft_companion_radio_usb_tcp_touch*.bin` filenames are still published
> as aliases, so devices already on an earlier beta keep self-updating.

Status: **beta**.
