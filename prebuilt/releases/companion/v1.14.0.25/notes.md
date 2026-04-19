## v1.14.0.25 — 2026-03-09

**Firmware version:** v1.14.0.25 (meshcomod on upstream 1.14+).

**Highlights:**
- **WSS:** delay(0) instead of delay(1) in handshake loop (yield only, no extra ms) so handshake finishes before browser closes; 40 steps per poll.
