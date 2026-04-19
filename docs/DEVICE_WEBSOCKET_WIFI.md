# Device WebSocket over Wi‑Fi (plain WS)

When TCP is enabled and Wi‑Fi has an address, the device serves the companion protocol over **plain WebSocket** (**`ws://`**) on the configured port (default **8765**).

Meshcomod **does not** implement **WSS** (`wss://`) on the device. That keeps the firmware smaller and leaves more **internal RAM** for **HTTPS URL OTA** (especially on boards without PSRAM).

## Default: `ws://`

- **Companion USB+TCP** builds: WebSocket is **`ws://<device-ip>:8765`** (or `WS_PORT` from the build).
- **Repeater TCP** builds: same — plain **`ws://`** only.

## Browsers and HTTPS (meshcomod.com)

- **HTTPS** pages (e.g. `https://meshcomod.com`) normally **block** `ws://` to a LAN IP (**mixed content**).
- Use the **HTTP**-hosted app when offered (e.g. **`http://app.meshcomod.com`**) so the browser may open **`ws://`** to your radio, or follow the in-app prompt for the HTTP mirror.
- You can still paste a full **`ws://192.168.x.x:8765`** URL when the page allows it.

## Optional: `wss://` in the client

The web client may still accept a manually entered **`wss://`** URL for **custom or third-party** firmware. **Stock meshcomod firmware will not answer** on TLS; use **`ws://`** to the device.

## Firmware implementation

- Server: [`src/helpers/esp32/WebSocketCompanionServer.cpp`](../src/helpers/esp32/WebSocketCompanionServer.cpp) — RFC 6455 upgrade and binary frames, `WiFiServer` only.

## References

- **Client:** Wi‑Fi connect flow in the meshcomod web app; IP:port handling prefers **`ws://`** for meshcomod devices when allowed by the page origin.
