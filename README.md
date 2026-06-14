<img width="1024" height="700" alt="meshcomod_logo_cropped" src="https://github.com/user-attachments/assets/5c4ff275-b306-4969-bb32-dd28298133c3" />

# meshcomod

A **multi-transport companion** firmware built on top of [MeshCore](https://github.com/meshcore-dev/MeshCore) for **Heltec** and **Seeed** LoRa devices (OLED / e-ink / headless). **One build serves USB + Bluetooth + TCP at the same time**, so you can drive the radio from a phone app, the web client, or Home Assistant — together.

Upstream: **[github.com/meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore)** (MeshCore is a lightweight multi-hop LoRa mesh; see their repo for the protocol, mobile clients, and the canonical flasher).

> **🖐️ Looking for the touch UI?** The standalone on-device LVGL interface for the **Heltec V4 TFT** and **LilyGo T-Deck** now lives in its own project: **[ALLFATHER-BV/wadamesh](https://github.com/ALLFATHER-BV/wadamesh)**. This repo is the OLED/e-ink companion firmware (the MeshCore core it builds on) only.

> **Experimental — use at your own risk.** This firmware is not officially supported. Flashing custom firmware may have unexpected effects; you are responsible for your use of it. No warranty is provided.

---

## What's different in meshcomod

A single firmware image supports **USB, Bluetooth, and TCP** companion connections at the same time (e.g. Home Assistant on USB, the MeshCore app on BLE, the web client on TCP). Use one, two, or all three; BLE and TCP can be toggled from the device UI.

- **USB** — always on when the device is powered.
- **Bluetooth** — the pairing PIN is shown on the device's **Bluetooth tab** (state + pairing info). **Long-press** the tab to enable/disable BLE; the footer reads "ON: long press" / "OFF: long press".
- **TCP** — server on port **5000**, multiple clients. The **Network (TCP) tab** shows status, IP, port, and SSID. **Long-press** to enable/disable TCP. Wi-Fi credentials are **not** stored in the repo — set them at build time (`WIFI_SSID` / `WIFI_PWD`) or at runtime (see [Configure Wi-Fi](#configure-wi-fi)). The plain WebSocket port is **8765** (`ws://` only).

<img width="190" height="450" alt="Toggle BLE & TCP on the device" src="https://github.com/user-attachments/assets/671345e3-dd68-48a7-b3fe-6ebdacdc425f" />

Other niceties:

- **Push to all clients** — RX log, new messages, contact adverts, and path updates are sent to **every** connected client, so each app sees live updates.
- **No duplicate RX log on sync** — when one client syncs history, only that client gets the sync frames; the others don't see them re-echoed.

Otherwise this is the same codebase as MeshCore; we sync from upstream and add our addon customizations on top.

### Supported devices

| Device | MCU | Display | Build env |
|---|---|---|---|
| **Heltec WiFi LoRa 32 V4** | ESP32-S3 | 128×64 OLED | `heltec_v4_companion_radio_usb_tcp` |
| **Heltec WiFi LoRa 32 V3** | ESP32-S3 | 128×64 OLED | `Heltec_v3_companion_radio_usb_tcp` |
| **Heltec Wireless Paper** | ESP32-S3 | 213×104 e-ink | `Heltec_Wireless_Paper_companion_radio_usb_tcp` |
| **Seeed Xiao S3 WIO** | ESP32-S3 | optional 128×64 OLED | `Xiao_S3_WIO_companion_radio_usb_tcp` |

**Env-name casing matters when building:** V4 uses lowercase `heltec_v4_…`; V3 uses a capital H `Heltec_v3_…`. Use the exact name (see [Build it yourself](#hard--build-it-yourself)).

> **Touch boards** (Heltec V4 TFT, LilyGo T-Deck) are built and released from **[wadamesh](https://github.com/ALLFATHER-BV/wadamesh)**, not here.

<p align="left">
  <img width="200" height="200" alt="Heltec WiFi LoRa 32 V4" src="https://github.com/user-attachments/assets/1ecd81c5-644b-4df3-99a8-e544d1864a01" />
  <img width="200" height="200" alt="Heltec WiFi LoRa 32 V3" src="https://github.com/user-attachments/assets/21289b67-2e1f-496e-8d9f-65c36ee74461" />
</p>

---

## Home Assistant tab

Run the **full meshcomod web client as a tab inside Home Assistant**. Connect the device over **TCP (Wi-Fi)** or **USB** to the machine running HA, then add the [meshcomod client](https://meshcomod.com) as an iframe/panel for a dedicated **MESHCOMOD** sidebar tab: voltage, state of charge, temperature, uptime, and realtime/trend graphs alongside your other dashboards.

<p align="left">
  <img width="700" alt="Meshcomod client as a Home Assistant tab" src="docs/meshcomod-ha-tab.png" />
</p>

> **Use TCP for stability in Home Assistant.** The web console and the companion protocol share the USB serial port, so a USB-connected HA can be affected by console handling. **Connect HA over TCP (Wi-Fi)** for a separate, unaffected path.

---

## Configure Wi-Fi

Three ways, no CLI recovery mode needed. **Wi-Fi is 2.4 GHz only** (not 5 GHz-only SSIDs); credentials are stored in NVS and survive reboots.

**1. Flasher Wi-Fi GUI (easiest)** — the Wi-Fi panel on [flasher.meshcomod.com](https://flasher.meshcomod.com).

**2. Web/USB console** — [console.meshcomod.com](https://console.meshcomod.com) (or the Console tab on the flasher), device on USB:

```
set wifi.ssid YourSSID
set wifi.pwd  YourPassword     # open network: set wifi.pwd
wifi.apply
wifi.status
```

<img width="424" height="99" alt="console wifi setup" src="https://github.com/user-attachments/assets/ea127bb0-9f97-4f09-8538-31451bb9b939" />

**3. Meshcomod chat** — open the built-in **Meshcomod** contact (favourited by default, near the bottom of the contact list) and send commands. **These are handled locally on the device and are *not* sent over the LoRa mesh.** Note your client may keep the password in local chat history.

| Command | What it does |
|---|---|
| `help` | List all Meshcomod commands |
| `status` | USB/BLE/TCP and Wi-Fi state |
| `wifi set ssid "<v>"` | Set SSID (quote if it has spaces) |
| `wifi set pwd "<v>"` | Set password (`""` for open) |
| `wifi scan` / `wifi use <n>` | List nearby SSIDs, then pick one |
| `wifi status` | SSID, runtime vs compile-time creds, link state |
| `wifi apply` | Reconnect with stored credentials |
| `wifi clear` | Clear runtime creds (fall back to compile-time on next boot) |
| `tcp on` / `tcp off` / `tcp status` | Control/query the TCP server |
| `ble on` / `ble off` / `ble status` | Control/query Bluetooth |

> Disabling wireless access is guarded: `tcp off` and `ble off` ask you to confirm with `ok` (or `cancel`) first, since you could lock yourself out.

---

## Easy — flash a prebuilt

<p align="left">
  <img width="395" height="84" alt="Easy" src="https://github.com/user-attachments/assets/cd496321-1aad-425f-b8cf-ccba2cc9478d" />
</p>

No local build needed. Builds are published as **GitHub Releases** (tag `companion-*`); the latest is also mirrored to the web flasher.

1. Open **[flasher.meshcomod.com](https://flasher.meshcomod.com)**.
2. **Easy mode (recommended):** pick your device + version; the flasher downloads it for you.
3. **Manual mode:** choose **Custom firmware** and upload a `.bin` from the [latest release](https://github.com/ALLFATHER-BV/meshcomod/releases?q=companion).
4. Connect via USB, BLE (PIN on the display), and/or TCP.

> If unsure which binary to flash, use **merged** (it includes the bootloader + partition table and writes from `0x0`).

Pinned older versions for rollback: see the [Release log (RELEASES.md)](RELEASES.md) and the [GitHub Releases](https://github.com/ALLFATHER-BV/meshcomod/releases) list.

---

## Hard — build it yourself

<p align="left">
  <img width="395" height="84" alt="Hard" src="https://github.com/user-attachments/assets/92a0ea2d-e1f1-4e8b-82f0-705a1f826fd1" />
</p>

For custom code or build flags.

**Dependencies:** `git`, `python3` + `pip`, and `platformio` (`pio` in PATH, or `python3 -m platformio`). `build.sh` falls back to `python3 -m platformio` when `pio` isn't on PATH.

- macOS: `xcode-select --install`, then `python3 -m pip install --user -U platformio`
- Linux: `sudo apt update && sudo apt install -y git python3 python3-pip`, then the pip install
- Windows: use **WSL** (Ubuntu) and follow the Linux steps

Clone the repo and build with the exact env name. Wi-Fi credentials are baked in at build time (override at runtime later).

#### Heltec V4 (OLED)

```bash
export WIFI_SSID="YourNetworkName"
export WIFI_PWD="YourPassword"          # open network: export WIFI_PWD=""
export FIRMWARE_VERSION=v1.16.0.2
sh build.sh build-firmware heltec_v4_companion_radio_usb_tcp
```

#### Heltec V3 (note the capital H)

```bash
export WIFI_SSID="YourNetworkName"
export WIFI_PWD="YourPassword"
export FIRMWARE_VERSION=v1.16.0.2
sh build.sh build-firmware Heltec_v3_companion_radio_usb_tcp
```

Other targets use the same flow with their env name (`Heltec_Wireless_Paper_companion_radio_usb_tcp`, `Xiao_S3_WIO_companion_radio_usb_tcp`). Build all shipped companions at once with `sh build.sh build-meshcomod-companion-firmwares`.

**Outputs** (every target): app-only `out/<env>-<version>-<sha>.bin` and **merged** `out/<env>-<version>-<sha>-merged.bin` (flash from `0x0`). The merged image is also at `.pio/build/<env>/firmware-merged.bin`.

### Black screen after flashing

If the display stays black after flashing and resetting:

1. **Flash the merged image** — it includes the bootloader + partition table. Generate with `pio run -t mergebin -e <env>` → `.pio/build/<env>/firmware-merged.bin`. (Default `out/` copies are app-only.)
2. **Use the web flasher** — on [flasher.meshcomod.com](https://flasher.meshcomod.com), **Custom firmware** → upload the merged file (writes from `0x0`).
3. **Still black?** — upload the merged file again; if the flasher offers **erase**, erase first then upload (this wipes contacts and other data).
4. **Heltec V4 only:** if a merged image still gives a black screen (while V3 is fine), try the **non-merged** `.bin` at offset `0x10000` (the device must already have a valid bootloader + partition table). Newer firmware also adds a longer pre-display delay and watchdog feeds on first boot to improve merged-boot reliability.

---

## Releases & CI

Companion / repeater / room-server firmware is built by GitHub Actions and published as **GitHub Releases**; push a track tag (e.g. `companion-v1.16.0.2`) and the pipeline builds the shipped boards, publishes the release with the bins attached, and prunes to the newest 5. See [`docs/CI_RELEASES.md`](docs/CI_RELEASES.md).

---

## Docs

- [Companion protocol](docs/companion_protocol.md) · [CLI commands](docs/cli_commands.md) · [QR codes](docs/qr_codes.md)
- [Packet format](docs/packet_format.md) · [Payloads](docs/payloads.md) · [Stats binary frames](docs/stats_binary_frames.md)
- [Device WebSocket / Wi-Fi](docs/DEVICE_WEBSOCKET_WIFI.md) · [FAQ](docs/faq.md)
- [CI releases](docs/CI_RELEASES.md) · [Release process](docs/RELEASE_PROCEDURE.md) · [Release log](RELEASES.md)

---

## Syncing from upstream MeshCore

```bash
git remote add upstream https://github.com/meshcore-dev/MeshCore.git   # if not already added
git fetch upstream
git merge upstream/main
# resolve any conflicts, then:
git push origin main
```

Contributing to **upstream MeshCore**? Use their `dev` branch for PRs; open an Issue first for larger changes — see [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore).

### Known quirks

- **Legacy/no-channel clients** — a client that doesn't implement channel chat should ignore channel message frames (`0x08`, `0x11`).
- **Debug over USB** — serial terminals show binary companion frames on the USB transport; use BLE/TCP for app traffic if you need clean USB logs.
- **BLE first connect** — on the first BLE connection you may need to disconnect and reconnect once.
- **First Meshcomod `help`** — the first `help` in a fresh Meshcomod chat may not reply; send it again.
- **Web console retry** — a `get/set wifi.*` command may not show a reply on the first try; run it again.

---

## License

Same as MeshCore (see [license.txt](license.txt)). MeshCore is MIT.
