# meshcomod

**MeshCore addon for Heltec WiFi LoRa 32 V4** — an addon on top of [MeshCore](https://github.com/meshcore-dev/MeshCore) firmware, trimmed to this device only, with extra companion targets (USB+TCP).

Upstream: **[github.com/meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore)** (MeshCore is a lightweight multi-hop LoRa mesh; see their repo for full docs, clients, and flasher.)

> **Experimental — use at your own risk.** This firmware is not officially supported. Flashing custom firmware may have unexpected effects; you are responsible for your use of it. No warranty is provided.  

### Supported devices

- **Heltec WiFi LoRa 32 V4** (ESP32-S3, 128×64 OLED, LoRa)

---

## What's different in meshcomod

- **Heltec V4 USB + TCP companion**  
  Firmware that supports **simultaneous USB and TCP** companion connections (e.g. Home Assistant on USB, Web App or CLI on TCP).  
  - Build env: `heltec_v4_companion_radio_usb_tcp`  
  - TCP server on port **5000** (configurable), multiple clients  
  - WiFi credentials are **not** stored in the repo; set env vars `WIFI_SSID` and `WIFI_PWD` before building (see Build below).
  - Extra tab with network details

- **Features (multi-transport)**  
  - **Push to all clients** — RX log, new messages, contact adverts, path updates, and other unsolicited events are sent to **every** connected client (USB and all TCP), so each app sees live updates.  
  - **No duplicate RX log on sync** — When one client loads or syncs history, only that client gets the sync responses; the other clients do not see those frames again, so the RX log does not duplicate on the device that didn't trigger the sync.

- **Build script**  
  `build.sh` can use `python3 -m platformio` when `pio` is not in PATH.

Otherwise this is the same codebase as MeshCore; we sync from upstream and add our addon customizations on top.

### Known bugs

- **Sent messages** — Messages sent from one client may not appear on the other device (e.g. the other companion client) in all cases; the firmware pushes them to history and notifies all clients, but behaviour can depend on sync timing or the client app.
- **Channel messages** — Sent channel messages are not pushed to other clients (to avoid wrong channel / "text as sender" when a client has no channel support, e.g. HA). If a client without channel support syncs and receives channel messages from history (e.g. received over the air), it may show "Failed to sync" or display them incorrectly; such clients should ignore or skip channel message frame types.

---

## Build (Heltec V4 USB+TCP)

```bash
git clone https://github.com/ALLFATHER-BV/meshcomod.git
cd meshcomod
```

Set WiFi via **environment variables**:

```bash
export WIFI_SSID=YourNetworkName
export WIFI_PWD=YourPassword
export FIRMWARE_VERSION=v1.13.0
sh build.sh build-firmware heltec_v4_companion_radio_usb_tcp
python3 -m platformio run -t mergebin -e heltec_v4_companion_radio_usb_tcp
```

Flash the merged `.bin` from `.pio/build/heltec_v4_companion_radio_usb_tcp/firmware-merged.bin` using [flasher.meshcore.co.uk](https://flasher.meshcore.co.uk) (select **Custom firmware** and upload the merged file) or esptool at 0x0. Connect over USB as usual, or over TCP to the device's IP on port 5000.

**Black screen after flashing?** If the display stays black, you need to flash the **merged** firmware (the single `.bin` that includes bootloader and partitions). Flashing the merged image will **erase the device** and you will lose all contacts and other data currently on the companion — there is no way to preserve them when switching to merged. Use the merged build only when necessary (e.g. display not coming up with the normal flasher flow).

---

## Syncing from upstream MeshCore

```bash
git remote add upstream https://github.com/meshcore-dev/MeshCore.git   # if not already added
git fetch upstream
git merge upstream/main
# resolve any conflicts, then:
git push allfather main
```

---

## License

Same as MeshCore (see [license.txt](license.txt) in this repo). MeshCore is MIT.
