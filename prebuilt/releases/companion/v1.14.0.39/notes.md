## Companion firmware v1.14.0.39

Date: 2026-03-23

### Highlights
- **Companion OTA GitHub-first:** if the UI sends a meshcomod `firmware-download` URL, the firmware rewrites it back to the matching `raw.githubusercontent.com/ALLFATHER-BV/meshcomod/main/...` path and fetches the app bin directly from GitHub first.
- **Flasher fallback only:** if the direct GitHub fetch fails, OTA retries once against `https://flasher.meshcomod.com/firmware-download/...` for the same artifact.
- **Existing OTA safety checks stay in place:** merged-bin rejection, HTML/body signature rejection, OTA partition size preflight, and explicit write/truncation diagnostics remain active.

### Why this release exists
Companion OTA should work like repeater OTA even when the UI starts from a flasher link. This release removes that mismatch by resolving those meshcomod download links back to raw GitHub on-device before the first HTTP attempt.

### Binaries
- `heltec_v4_companion_radio_usb_tcp.bin`
- `heltec_v4_companion_radio_usb_tcp-merged.bin`
- `heltec_v4_tft_companion_radio_usb_tcp_touch.bin`
- `heltec_v4_tft_companion_radio_usb_tcp_touch-merged.bin`
- `Heltec_v3_companion_radio_usb_tcp.bin`
- `Heltec_v3_companion_radio_usb_tcp-merged.bin`
