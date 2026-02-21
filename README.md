# meshcomod

**Custom MeshCore firmware** — a spinoff of [MeshCore](https://github.com/meshcore-dev/MeshCore) with extra features and build targets.

Upstream: **[github.com/meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore)** (MeshCore is a lightweight multi-hop LoRa mesh; see their repo for full docs, clients, and flasher.)

> **Experimental — use at your own risk.** This firmware is not officially supported. Flashing custom firmware may have unexpected effects; you are responsible for your use of it. No warranty is provided.

---

## What’s different in meshcomod

- **Heltec V4 USB + TCP companion**  
  Firmware that supports **simultaneous USB and TCP** companion connections (e.g. Home Assistant on USB, Web App or CLI on TCP).  
  - Build env: `heltec_v4_companion_radio_usb_tcp`  
  - TCP server on port **5000** (configurable), multiple clients  
  - Set `WIFI_SSID` and `WIFI_PWD` in `variants/heltec_v4/platformio.ini` for that env, then build and flash  

- **Build script**  
  `build.sh` can use `python3 -m platformio` when `pio` is not in PATH.

Otherwise this is the same codebase as MeshCore; we sync from upstream and add our customizations on top.

---

## Build (Heltec V4 USB+TCP)

```bash
git clone https://github.com/ALLFATHER-BV/meshcomod.git
cd meshcomod
```

Set WiFi in `variants/heltec_v4/platformio.ini` for env `heltec_v4_companion_radio_usb_tcp`:

- `WIFI_SSID='"YourSSID"'`
- `WIFI_PWD='"YourPassword"'`

Then:

```bash
export FIRMWARE_VERSION=v1.13.0
sh build.sh build-firmware heltec_v4_companion_radio_usb_tcp
python3 -m platformio run -t mergebin -e heltec_v4_companion_radio_usb_tcp
```

Flash the merged `.bin` from `.pio/build/heltec_v4_companion_radio_usb_tcp/firmware-merged.bin` (e.g. [flasher.meshcore.co.uk](https://flasher.meshcore.co.uk) or esptool at 0x0). Connect over USB as usual, or over TCP to the device’s IP on port 5000.

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
