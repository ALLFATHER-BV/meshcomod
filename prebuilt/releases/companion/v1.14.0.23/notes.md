## v1.14.0.23 — 2026-03-09

**Firmware version:** v1.14.0.23 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** recv_timeout = NULL again (no select block) to fix ERR_CONNECTION_RESET; 40 handshake steps per poll; tickWssHandshake() at start of each loop() so TLS handshake advances twice per loop and completes before browser times out (reduces ERR_CONNECTION_CLOSED).
