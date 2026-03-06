# WebSocket frame debug logging (GetContacts)

To verify WS delivery of contact frames (code 2=START, 3=CONTACT, 4=END) and client id consistency:

1. **Enable logging:** Build with `-DWS_FRAME_DEBUG=1` (e.g. in `platformio.ini` for your env: `build_flags = -DWS_FRAME_DEBUG=1`).
2. **WiFi-only:** When debug is on, do **not** use Web Serial on the same device; use only the WiFi companion client. Serial is used for debug output (client id, code, len, written).
3. **Run GetContacts over WiFi** and watch Serial (e.g. USB console):
   - You should see `WS frame client=X code=2 len=... written=...` for START.
   - Then `code=3` for each CONTACT (same `client=X`).
   - Then `code=4` for END (same `client=X`).
4. **Interpret:**
   - If START (2) appears but no CONTACT (3) or END (4): reply target or iterator is wrong before WS (e.g. CONTACT/END never passed to `writeToClient`).
   - If CONTACT/END appear with different `client=` than START: reply-target pinning bug.
   - If CONTACT/END show `written=0`: WS send or backpressure failing; retry loop should keep trying (check retries in MyMesh contact drain).

No transport filter: `MultiTransportCompanionInterface::writeFrame` does not inspect frame code; all codes go to `_ws.writeToClient(_last_reply_target - REPLY_TARGET_WS_0, src, len)`.
