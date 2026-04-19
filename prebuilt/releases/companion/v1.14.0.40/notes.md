## Companion firmware v1.14.0.40

Date: 2026-03-23

### Highlights
- **Companion HTTPS OTA fixed:** before URL OTA starts, companion now fully releases BLE when the active client is not BLE, reclaiming enough internal RAM for the ESP32 TLS client to connect and fetch firmware successfully.
- **Root cause captured in logs:** OTA diagnostics now report TLS errors plus `heap`, `max`, `psram`, and `pmax`, which made it clear the failure was internal-RAM allocation pressure rather than a bad URL or remote refusal.
- **Lower TLS footprint:** companion builds now use asymmetric mbedTLS content lengths so outgoing TLS buffers consume less RAM while preserving the incoming path needed for firmware downloads.

### Root cause
Companion OTA was failing before `HTTP OK` with `HTTP -1`, but deeper diagnostics showed the real error was `tls=-32512 SSL - Memory allocation failed`. Total free heap looked nonzero, yet the largest allocatable internal block was too small for the TLS client while BLE remained active. Releasing BLE before OTA resolved the issue.

### Binaries
- `heltec_v4_companion_radio_usb_tcp.bin`
- `heltec_v4_companion_radio_usb_tcp-merged.bin`
- `heltec_v4_tft_companion_radio_usb_tcp_touch.bin`
- `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin`
- `Heltec_v3_companion_radio_usb_tcp.bin`
- `Heltec_v3_companion_radio_usb_tcp-merged.bin`
