# Device WebSocket over WiFi (WSS)

When TCP is enabled and WiFi is up, the device serves the companion protocol over **WebSocket over TLS (wss://)** so a browser on **HTTPS** (e.g. https://meshcomod.com) can connect with **no extra software** (no bridge, no ngrok, no Tailscale).

## Default: WSS (wss://)

- **Companion USB+TCP** firmware is built with **WSS** by default (`WS_USE_TLS=1` in the variant `platformio.ini`).
- **Port:** 8765 (or `WS_PORT` from build).
- **URL:** `wss://<device-ip>:8765`
- Use from **HTTPS** pages; the first connection shows a certificate warning (self-signed) — click **Advanced** → **Proceed** once.
- **Certificate:** The firmware uses a **self-signed** certificate (and key) built in; see `src/helpers/esp32/wss_cert.h`. CN is `meshcomod.local`.

## Optional: plain WS (ws://) build

- To build **without** TLS (plain `ws://` only), remove or comment out `-D WS_USE_TLS=1` in the companion env in `variants/heltec_v4/platformio.ini` and `variants/heltec_v3/platformio.ini`. Then use from **HTTP** or localhost only (browsers block `ws://` from HTTPS).

### Client behaviour

- On **HTTPS**, when the user enters **IP:port** (e.g. `192.168.20.230:8765`), the client connects to **`wss://<ip>:8765`**. Default firmware is WSS-only on that port.
- On HTTP or localhost, the client could use **`ws://`** — but the default build has **WSS only** (`WS_USE_TLS=1`), so the device accepts **only** `wss://` on port 8765.

### First connection: certificate warning

- The first time you connect to `wss://<device-ip>:8765`, the browser will show a **certificate warning** (self-signed / untrusted).
- The user must click **Advanced** → **Proceed to &lt;address&gt;** (or equivalent) **once**. After that, the connection works from HTTPS with no extra tools.

### Checklist (firmware)

- [x] TLS server listen on the WebSocket port (e.g. 8765) when `WS_USE_TLS=1`.
- [x] Load self-signed certificate and private key (embedded in `wss_cert.h`).
- [x] After TLS handshake, perform WebSocket HTTP Upgrade and binary framing as today.
- [x] Document for users: “First connection to the device from HTTPS may show a certificate warning; accept it once.”

### References

- **Client:** When on HTTPS, connect to `wss://${host}:${port}` for IP:port input.
- **Firmware:** [meshcomod](https://github.com/ALLFATHER-BV/meshcomod) — WSS behind `WS_USE_TLS=1` in `WebSocketCompanionServer` and `wss_cert.h`.
