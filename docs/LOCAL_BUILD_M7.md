# Elecrow ThinkNode M7

The M7 is an ESP32-S3 + LR1110 board with a 100 Mbps RJ45 (WCH CH390 SPI Ethernet
controller) and optional PoE. `variants/thinknode_m7/` and `boards/thinknode_m7.json`
arrived with the upstream 1.17.0 merge, but the board is not in meshcomod's release matrix,
so there is no prebuilt binary and no web-flasher entry (see [CI releases](CI_RELEASES.md)).
It has to be built from source.

## Environments

`*_companion_radio_usb_tcp` (meshcomod's USB + TCP + WebSocket companion) does not exist for
this board, so these are upstream's environments.

| Env | Transports | Notes |
|-----|-----------|-------|
| `ThinkNode_M7_companion_radio_ethernet` | USB + Ethernet | The PoE wired node. Companion TCP on port 5000 over the wire, USB for config. |
| `ThinkNode_M7_companion_radio_ble` | BLE | Ethernet flags are set but unused, since BLE takes precedence. See "Remaining gaps". |
| `ThinkNode_M7_companion_radio_usb` | USB | Simplest bring-up target. |
| `ThinkNode_M7_companion_radio_wifi` | Wi-Fi (TCP 5000) | SSID/password hardcoded in the variant's `platformio.ini`. |
| `ThinkNode_M7_kiss_modem` | USB | KISS TNC. |
| `ThinkNode_M7_repeater` | none | **Does not build.** See "Remaining gaps". |

## Building

```bash
export FIRMWARE_VERSION=v1.17.0
MERGE_BIN=1 bash build.sh build-firmware ThinkNode_M7_companion_radio_ethernet
```

`MERGE_BIN=1` is needed because `build.sh` only produces a merged image automatically for env
names in the release matrix, and no M7 env matches. Without it you get the app image only.

| Output | Flash at | Contents |
|--------|----------|----------|
| `out/<env>-<version>-<sha>-merged.bin` | `0x0` | bootloader + partition table + app |
| `out/<env>-<version>-<sha>.bin` | `0x10000` | app only, keeps partition table and SPIFFS |

Flash with [esptool-js](https://espressif.github.io/esptool-js/) in Chrome, or:

```bash
esptool --chip esp32s3 --port <PORT> --baud 921600 write-flash 0x0 out/<file>-merged.bin
```

The board enumerates as a WCH CH343 bridge (`1a86:7522`), not Espressif native USB, so `<PORT>`
is typically `/dev/ttyUSB0` on Linux (ch341 driver) or `COMx` on Windows, not `/dev/ttyACM0`.
Note also that
`boards/thinknode_m7.json` still declares `hwids` of `0x303A:0x1001`, which breaks PlatformIO's
automatic port detection. Serial console is 115200. There is no reset button, and the auto-reset
circuit works, so pulse DTR/RTS (or power cycle) to reset.

A merged image spans `0x0` to `~0x131000`, which covers NVS at `0x9000` but **not** SPIFFS at
`0x670000`. Node preferences live in `/prefs.json` in SPIFFS, so radio settings survive
reflashing.

## Radio region

`[arduino_base]` in `platformio.ini` compiles in EU defaults (869.618 MHz / 62.5 kHz / SF8).
These seed the factory defaults only: companion firmware copies them into `_prefs` on first
boot and uses stored preferences thereafter, and a client app will typically overwrite them
during setup. For the headless roles (repeater, room server, KISS modem) there is no app, so
the compiled values are the operating configuration. Set them explicitly when building those
for a non-EU region.

## Hardware validation

Verified on a real ThinkNode M7 (this is the first known instance of meshcomod running on the
board):

- Boots, reporting `[BOOT] board ok` and `[BOOT] radio ok`, so the LR1110 initializes.
- BLE companion pairs from the MeshCore client.
- USB companion connects from the web client.
- Ethernet: link, DHCP lease, `ETH: listening on TCP port: 5000`, USB live simultaneously.
- Home Assistant connects over the Ethernet TCP transport and reads telemetry.
- Both LEDs work. Green shows the firmware heartbeat, a 20 ms blip every 4 s that lengthens to
  200 ms while messages are unread. Blue follows LoRa transmit.
- The compiled radio defaults seed a fresh device. After the store was cleared the node came up
  on 927.875 MHz, 62.5 kHz, SF7, CR5 with no client involvement.
- DHCP-to-DNS registration works. A rename propagated to DNS with no measurable delay, on the
  same MAC and the same lease.

## Remaining gaps

- **`ThinkNode_M7_repeater` does not compile.** `examples/simple_repeater/UITask.cpp` uses
  `DisplayDriver::BLUE` and `DisplayDriver::LIGHT`, but that `enum Color` is commented out at
  `src/helpers/ui/DisplayDriver.h:20`, superseded by the `UIColor` statics. This breaks the
  plain `*_repeater` envs on **every** board, reproduced identically on
  `Station_G3_ESP32_repeater`, so it is not M7-specific and wants its own fix.
- **`ThinkNode_M7_companion_radio_ble` has unused Ethernet.** The env sets both `BLE_PIN_CODE`
  and the CH390 flags, but the companion registers only BLE. `MultiSerialInterface` supports
  registering BLE, USB and Ethernet together, and wiring that up would give the board every
  transport at once.
- **`examples/companion_radio/ui-tiny/UITask.h`** still declares its constructor as taking
  `MultiSerialInterface*`, which nothing constructs on nRF52. `LilyGo_T-Echo_Card_companion_radio_ble`
  and `_usb` carry the same break that this change fixes for `ui-orig`.
- **`boards/thinknode_m7.json` `hwids`** do not match the hardware (see above).
- **The ethernet MAC is locally administered.** This platform builds with
  `CONFIG_ESP32S3_UNIVERSAL_MAC_ADDRESSES=2`, so the ESP-IDF derives the ethernet address from
  the Bluetooth address with the locally administered bit set. From an illustrative base of
  `00:00:5E:00:53:00` that gives Bluetooth `00:00:5E:00:53:01` and ethernet `02:00:5E:00:53:01`,
  which differs from the base in both the first and the last octet and carries no vendor OUI.
  ESPHome on the same board reports `00:00:5E:00:53:03`, base+3, because it builds with four
  universal addresses and assigns the ethernet address explicitly with
  `esp_read_mac(ESP_MAC_ETH)` and `esp_eth_ioctl(ETH_CMD_S_MAC_ADDR)` before attaching the
  netif. Matching that needs the universal address count changed, since
  `esp_read_mac(ESP_MAC_ETH)` returns the derived value here and writing it back is a no-op.
  Changing it moves the address of every deployed unit, discarding whatever the current one is
  bound to, so it is left alone.
- **A mains powered device reports a battery.** `ESP32Board::getBattMilliVolts()` returns 0
  unless `PIN_VBAT_READ` is defined, and the M7 defines no such pin because the board is powered
  over PoE or USB and carries no cell. Zero is indistinguishable from a flat battery, so every
  client presents the node as critically discharged. The protocol has no way to say a device has
  no battery, which is where the fix belongs: a device with no cell should report absence rather
  than zero, and a board should be able to declare that it is externally powered.
- **The Home Assistant integration exposes battery entities for a device with no battery.** It
  creates Battery Percentage and Battery Voltage from that zero, so the node shows 0 percent with
  a low-battery icon and will drive any automation or alert keyed on battery level. Until the
  protocol can express absence, those entities should be omitted or reported unavailable for a
  device that reports no cell rather than published as a real reading.
- **The meshcomod web client mis-parses the device info frame, and the offset is known.** Byte 2
  of `RESP_CODE_DEVICE_INFO` is `MAX_CONTACTS / 2`, 175, which is invalid UTF-8, and byte 3 is
  `MAX_GROUP_CHANNELS`, 40. Those are what the mojibake and the stray bracket are. Bytes 8 to 19
  hold the build date and bytes 20 on the manufacturer name, which is why they appear run
  together. The client starts reading at offset 2 rather than offset 8, skipping the two byte
  header but neither the two v3+ count bytes nor the four byte BLE pin, and does not stop at the
  null separators. The same client reads `Device model` and `Firmware` correctly from that frame,
  and Home Assistant renders it correctly, so the frame is well formed and `StrHelper::strzcpy`
  pads every field. The fix belongs in that client.
- **The same client reports a battery voltage the firmware never sent.** It has shown 7.04 V,
  2.56 V and 0.000 V for a board with no cell, where Home Assistant renders 0.000 V from the same
  device. The firmware reports zero, so the value is invented by that client's parsing and the
  fix belongs with the frame offset above.
