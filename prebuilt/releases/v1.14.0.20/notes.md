## v1.14.0.20 — 2026-03-09

**Firmware version:** v1.14.0.20 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** 100ms read timeout + recv_timeout callback so TLS handshake receives client data; fixes ERR_CONNECTION_CLOSED for https://device:8765 and wss://.
- **WSS:** TLS 1.2 minimum for browser compatibility; extended WebSocket payload length fix (TLS path); EOF handling in doHandshake and pollRecvFrame.
- **Heltec V3:** Declare `set_boot_phase` in variant target.h so V3 companion build succeeds.
