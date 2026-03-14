# Heltec V3 boot-loop debug build

If your **Heltec WiFi LoRa 32 V3** gets stuck in a boot loop (e.g. shows the boot logo then hangs or resets), use this debug firmware to see where it stops. It prints verbose logs over **USB serial** so you can capture the last messages before the hang.

## 1. Flash the debug build

- **Merged image (recommended):** Use the file **`Heltec_v3_BOOT_DEBUG_FLASH_THIS-merged.bin`** in the MeshCore repo root (or build your own — see below).
- Flash it the same way as normal meshcomod firmware, e.g. [flasher.meshcomod.com](https://flasher.meshcomod.com) → **Custom firmware** → upload this merged `.bin`. Use full erase if the device is unstable.

## 2. Open USB serial **before** powering or resetting

- Connect the V3 over USB.
- Open a serial console at **115200 baud**:
  - **Web:** [console.meshcomod.com](https://console.meshcomod.com) or the Console tab on the flasher — select the V3’s serial port, set 115200.
  - **Desktop:** PuTTY, screen, or PlatformIO Serial Monitor at 115200.
- Then power the device or press reset. The debug build prints mesh, WiFi, BLE, and core logs from early boot. **Capture or note the last lines** before it stops or reboots — that shows where the boot is failing.

## 3. What to capture

- Copy or screenshot the **last 20–50 lines** of serial output when the device gets stuck or just before it resets.
- Share that with the meshcomod maintainers (e.g. GitHub issue or your usual channel). The log will show which init step (display, WiFi, TCP, BLE, mesh, etc.) is hanging or crashing.

## Building the debug image yourself

From the MeshCore repo:

```bash
cd MeshCore
export FIRMWARE_VERSION=v1.14.0.29-boot-debug
export WIFI_SSID="YourSSID"   # optional, for WiFi init logs
export WIFI_PWD="YourPassword"
sh build.sh build-firmware Heltec_v3_companion_radio_usb_tcp_boot_debug
```

The merged image is written to:

`out/Heltec_v3_companion_radio_usb_tcp_boot_debug-<version>-<sha>-merged.bin`

Copy it to a convenient name (e.g. `Heltec_v3_BOOT_DEBUG_FLASH_THIS-merged.bin`) and flash that file.

## What this build enables

- **MESH_DEBUG=1** — mesh/routing debug messages
- **BLE_DEBUG_LOGGING=1** — Bluetooth stack logs
- **WIFI_DEBUG_LOGGING=1** — WiFi/connection logs  
- **CORE_DEBUG_LEVEL=5** — ESP32 core verbose logs

Serial remains at **115200** baud. After troubleshooting, re-flash a normal (non-debug) meshcomod build from [RELEASES.md](RELEASES.md).
