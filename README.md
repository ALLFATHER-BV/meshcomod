# meshcomod

**MeshCore addon for Heltec WiFi LoRa 32 V4** — an addon on top of [MeshCore](https://github.com/meshcore-dev/MeshCore) firmware, trimmed to this device only. **One build supports three companion transports: USB, Bluetooth, and TCP** (choose any combination; toggle BLE and TCP from the device UI).

Upstream: **[github.com/meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore)** (MeshCore is a lightweight multi-hop LoRa mesh; see their repo for full docs, clients, and flasher.)

> **Experimental — use at your own risk.** This firmware is not officially supported. Flashing custom firmware may have unexpected effects; you are responsible for your use of it. No warranty is provided.  

### Supported devices

- **Heltec WiFi LoRa 32 V4** (ESP32-S3, 128×64 OLED, LoRa)
- **Heltec WiFi LoRa 32 V3** (ESP32, 128×64 OLED, LoRa)

**Build env names differ by device:** V4 uses lowercase `heltec_v4_...`; V3 uses a capital H: `Heltec_v3_...`. Use the exact env name when building (see examples below).

---

## What's different in meshcomod

- **Heltec V4 & V3: USB + Bluetooth + TCP in one build**  
  A single firmware image supports **USB, Bluetooth, and TCP** companion connections at the same time (e.g. Home Assistant on USB, MeshCore app on BLE, Web/CLI on TCP). You can use one, two, or all three; BLE and TCP can be turned on or off from the device UI.

  - V4 build env: `heltec_v4_companion_radio_usb_tcp` (lowercase)  
  - V3 build env: `Heltec_v3_companion_radio_usb_tcp` (capital H)  
  - **USB** — always on when the device is on.  
  - **Bluetooth** — default PIN **123456** (configurable via `BLE_PIN_CODE`). **Bluetooth tab**: shows "BLE", pairing PIN when on, or "BLE disabled". **Long press** on this tab to enable or disable BLE; footer shows "ON: long press" / "OFF: long press".  
  - **TCP** — server on port **5000** (configurable), multiple clients. WiFi credentials are **not** stored in the repo; set env vars `WIFI_SSID` and `WIFI_PWD` before building (see Build below). **Network (TCP) tab**: shows TCP status, IP, port, and **SSID**. **Long press** on this tab to enable or disable TCP; footer shows "ON: long press" / "OFF: long press".

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

## Build (USB + Bluetooth + TCP)

Clone and enter the repo:

```bash
git clone https://github.com/ALLFATHER-BV/meshcomod.git
cd meshcomod
```

Set **WiFi** and **firmware version** via environment variables, then run the build. The script runs merge and copies the merged image into `out/`. Use the **exact** env name for your device (V4 = lowercase `heltec_v4_...`, V3 = capital H `Heltec_v3_...`).

---

### Heltec V4 (copy-paste)

```bash
export WIFI_SSID=YourNetworkName
export WIFI_PWD=YourPassword
export FIRMWARE_VERSION=v1.13.0
sh build.sh build-firmware heltec_v4_companion_radio_usb_tcp
```

Merged image: `out/heltec_v4_companion_radio_usb_tcp-<version>-<sha>-merged.bin` or `.pio/build/heltec_v4_companion_radio_usb_tcp/firmware-merged.bin`.

---

### Heltec V3 (copy-paste)

```bash
export WIFI_SSID=YourNetworkName
export WIFI_PWD=YourPassword
export FIRMWARE_VERSION=v1.13.0
sh build.sh build-firmware Heltec_v3_companion_radio_usb_tcp
```

Merged image: `out/Heltec_v3_companion_radio_usb_tcp-<version>-<sha>-merged.bin` or `.pio/build/Heltec_v3_companion_radio_usb_tcp/firmware-merged.bin`.

---

Flash using **[flasher.meshcore.co.uk](https://flasher.meshcore.co.uk)**: select **Custom firmware** and upload your merged `.bin`. Connect over **USB**, **Bluetooth** (PIN 123456), and/or **TCP** (device IP, port 5000).

> **First time flashing meshcomod?** You must use the **merged** `.bin` (the single file from `out/` or `.pio/build/<env>/firmware-merged.bin`). The app-only build is not suitable for an initial flash.

### Black screen after flashing (V3 and V4)

If the display stays black after flashing and resetting:

1. **Use the merged firmware** — You must flash the **merged** `.bin` (the single file that includes bootloader and partition table), not the app-only build. The build script puts it in `out/<env>-<version>-<sha>-merged.bin` or `.pio/build/<env>/firmware-merged.bin`.
2. **Flash with the web flasher** — On [flasher.meshcore.co.uk](https://flasher.meshcore.co.uk), choose **Custom firmware** and upload the merged file. The flasher uses the correct layout (writes from 0x0).
3. **If it’s still black** — Try uploading the merged file again via **Custom firmware**. If the flasher offers an erase option, use it first, then upload the merged `.bin`. This wipes all data (contacts, etc.).

Flashing the merged image will **erase the device** and you will lose all contacts and other data. Use the merged build when the display does not come up with the normal flasher flow.

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
