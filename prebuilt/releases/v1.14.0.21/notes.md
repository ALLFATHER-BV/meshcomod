## v1.14.0.21 — 2026-03-09

**Firmware version:** v1.14.0.21 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** recv_timeout = NULL again so TLS handshake never blocks; fixes ERR_CONNECTION_RESET when opening https://device:8765 and wss://. (Removed blocking select() callback that caused resets.)
- **WSS:** TLS 1.2 minimum, extended payload length fix (TLS path), EOF handling retained.
