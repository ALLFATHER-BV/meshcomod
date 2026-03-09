## v1.14.0.24 — 2026-03-09

**Firmware version:** v1.14.0.24 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** delay(1) on WANT_READ/WANT_WRITE in handshake loop so WiFi stack can deliver data (avoids BEACON_TIMEOUT / ERR_CONNECTION_RESET).
- **WSS:** 30s handshake timeout per client to close stuck connections (ESP-IDF #14999).
