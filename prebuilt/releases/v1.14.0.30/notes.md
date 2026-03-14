## v1.14.0.30 — 2026-03-14

**Firmware version:** v1.14.0.30 (meshcomod on upstream 1.14+).

**Highlights:**
- **V3 boot fix:** Prevents crash/reboot when device has no WiFi credentials (first boot or NVS erase). WiFi/tcpip stack is always started so the TCP server can bind without triggering LwIP "Invalid mbox" assert.
- **NVS:** Creates the `meshcomod` Preferences namespace on first boot when missing, eliminating repeated `nvs_open failed: NOT_FOUND` errors.
