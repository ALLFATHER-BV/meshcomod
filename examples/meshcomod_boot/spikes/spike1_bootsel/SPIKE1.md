# Spike 1 — bootloader-stage recovery selection

**Question this answers:** on the real T-Deck, will the stock ESP-IDF bootloader
divert the boot into a separate *recovery* app when a GPIO is held — i.e. can we
reach recovery even if the main firmware is dead? This is the single hardware-only
unknown behind the whole "recovery, not just a launcher" design. Everything else
(`esp_ota_set_boot_partition`, rollback) is already-proven IDF API.

## What it builds

One tiny ESP-IDF app, flashed to **two** app partitions:

| Partition | Subtype   | Role in the spike |
|-----------|-----------|-------------------|
| `factory` | `factory` | the normal app — **MAIN** |
| `test`    | `test`    | the GPIO-triggered app — **RECOVERY** |

`sdkconfig.defaults` enables `CONFIG_BOOTLOADER_APP_TEST` on **GPIO0** (active-low),
which is the T-Deck's **trackball click**. The app just prints which partition it is
running from.

## Build & flash

```bash
# from this folder
ls /dev/cu.usbmodem*                 # find the T-Deck's port
bash flash-spike1.sh /dev/cu.usbmodemXXXX
/Users/kaj/Library/Python/3.9/bin/pio device monitor -p /dev/cu.usbmodemXXXX -b 115200
```

(Or build only: `/Users/kaj/Library/Python/3.9/bin/pio run -e spike1_tdeck`.)

## The test — PASS / FAIL

1. **Normal boot.** Tap reset and let go.
   → serial prints `Booted partition: MAIN (factory)`.
2. **Recovery gesture.** Tap reset, **release**, then **press and hold the trackball
   click for ≥5 s**.
   → serial prints `Booted partition: RECOVERY (test)  <-- GPIO divert worked!`.

If step 2 lands in RECOVERY, **Spike 1 passes**: we have a hardware path into recovery
that does not depend on the main firmware being alive.

## Important GPIO0 caveat (read this)

GPIO0 is **also the USB-download strap**. If you hold the trackball *through* the
reset, the chip enters **USB download mode** instead of running our bootloader. That's
why the gesture is *reset → release → then hold*. The bootloader samples GPIO0 for the
first ~5 s after it starts, so begin holding right after the reset blip.

If the GPIO0 timing is fiddly and you just want to confirm the *feature* first, edit
`sdkconfig.defaults`:

```
CONFIG_BOOTLOADER_NUM_PIN_APP_TEST=16     # any spare, non-strapping GPIO
```

rebuild/reflash, and simply jumper that pin to GND at boot. Then switch back to GPIO0
for the real on-device gesture.

## If it passes / fails

- **Pass:** L0 in `docs/ARCHITECTURE.md` is confirmed as *stock bootloader + sdkconfig +
  a small boot-intent helper* — no custom bootloader needed for v1. Next is Spike 2
  (load + execute the recovery UI from the SD card) only if we want SD-resident recovery
  beyond the built-ins; otherwise we go straight to scaffolding the real hybrid project.
- **Fail / weird:** capture the serial log and we revisit — likely a GPIO/strap detail or
  a `sdkconfig` option name to adjust for this exact IDF version.
