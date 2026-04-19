## v1.14.0.22 — 2026-03-09

**Firmware version:** v1.14.0.22 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** 15ms recv_timeout + 8 handshake steps per poll so TLS handshake receives client flights without long block; avoids ERR_CONNECTION_CLOSED while keeping RESET at bay.
