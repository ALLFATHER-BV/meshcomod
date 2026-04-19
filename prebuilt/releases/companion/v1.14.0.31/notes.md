## v1.14.0.31 — 2026-03-14

**Firmware version:** v1.14.0.31 (meshcomod on upstream 1.14+).

**Highlights:**
- **WiFi/NVS logs no longer in public chat:** Only read `wifi_ssid` / `wifi_pwd` from Preferences when the key exists (`isKey()` first). Avoids Arduino Preferences `getString()` logging "nvs_get_str len fail: wifi_ssid NOT_FOUND" to Serial, which was being forwarded into the mesh and shown in Channel 0.
