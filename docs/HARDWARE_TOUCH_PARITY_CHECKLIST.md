# Heltec V4 touch UI — hardware parity acceptance checklist

Run on **Heltec V4 + capacitive touch** build with `-D UI_LVGL` and `-D HAS_HELTEC_V4_CAP_TOUCH` (see `variants/heltec_v4/platformio.ini`).

## Boot & shell

- [ ] Device boots without watchdog loop; splash/home appears within a few seconds.
- [ ] LVGL flush uses shared ST7789 path (no second SPI display init).

## Navigation

- [ ] Bottom tabs: **Home**, **Chat**, **Chan**, **Net**, **Set** respond to tap.
- [ ] Horizontal swipe changes tab; vertical swipe scrolls active tab content.
- [ ] While a **chat detail** or **settings section** overlay is open, horizontal swipe does **not** change tab (overlays close via back or tab switch).
- [ ] After reboot, **last selected tab** restores (NVS `meshTouch`).

## Chat (DM)

- [ ] Contact list on **Net** shows mesh contacts; tapping a row opens **Chat** on that thread.
- [ ] Sending from composer delivers **DM** via RF (or queues mesh send); alert shows **Sent** / **Send failed** / **No contact match** as appropriate.
- [ ] Incoming DM appears in timeline and increments unread when not on that thread.

## Channels

- [ ] **Chan** tab lists channel threads seeded from device channel slots (named slots).
- [ ] Sending on a resolved channel slot calls group send; failure shows alert.

## Network / repeater-facing

- [ ] **Net** tab shows contacts + connection hint; **Set → Transports** holds TCP / BLE / Wi‑Fi toggles, WS status text, and **Reset path (active DM)** when applicable.

## Settings & diagnostics (webclient-style)

- [ ] **Set** tab shows six category rows: Profile, Radio, Auto-add, Transports, Device, Experimental; each opens a full-page overlay with back affordance.
- [ ] **Profile / Radio / Auto-add / Experimental:** Save persists via `savePrefs()`; radio save warns and can reboot (same idea as web client).
- [ ] **Transports:** TCP/BLE match `_serial` state; Wi‑Fi toggle matches meshcomod `wifi on`/`wifi off` behaviour on Wi‑Fi-enabled builds.
- [ ] **Device:** GPS / buzzer switches persist; diagnostics rolling log visible in this overlay.

## Regression

- [ ] With USB/BLE client connected, companion commands still work unchanged (no protocol regression).
- [ ] Battery / unread summary on Home updates periodically.

## Sign-off

| Date | Firmware version | Tester | Pass/Fail |
|------|------------------|--------|-----------|
|      |                  |        |           |
