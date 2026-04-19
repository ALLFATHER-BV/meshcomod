## r1.15.0.2 — 2026-04-19

**Repeater TCP version string (on device):** `meshcomod-r1.15.0.2-repeater-tcp-<gitsha>`.

**Highlights**

- **1.15 line:** Default compile-time repeater TCP labels in PlatformIO are **`v1.15.0.2-repeater-tcp`** (V4 OLED, V4 TFT, V3, Wireless Paper). Plain mesh Heltec V4 repeaters (non-TCP) use **`v1.15.0.0-repeater`** / **`v1.15.0.0-repeater-tft`**.
- **Full TCP matrix:** This drop includes **V4 OLED**, **V4 TFT+touch**, **V3**, and **Heltec Wireless Paper** `*_repeater_tcp` app and merged images (see table).

**Prebuilts**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_repeater_tcp-merged.bin](heltec_v4_repeater_tcp-merged.bin) | [heltec_v4_repeater_tcp.bin](heltec_v4_repeater_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_repeater_tcp-merged.bin](heltec_v4_tft_repeater_tcp-merged.bin) | [heltec_v4_tft_repeater_tcp.bin](heltec_v4_tft_repeater_tcp.bin) |
| Heltec V3 | [Heltec_v3_repeater_tcp-merged.bin](Heltec_v3_repeater_tcp-merged.bin) | [Heltec_v3_repeater_tcp.bin](Heltec_v3_repeater_tcp.bin) |
| Heltec Wireless Paper (E213) | [Heltec_Wireless_Paper_repeater_tcp-merged.bin](Heltec_Wireless_Paper_repeater_tcp-merged.bin) | [Heltec_Wireless_Paper_repeater_tcp.bin](Heltec_Wireless_Paper_repeater_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `REPEATER_FIRMWARE_VERSION=r1.15.0.2-repeater-tcp`, `sh build.sh build-repeater-firmwares`, git **`2c430597`** (short SHA embedded in `out/` filenames for this promotion).

**Procedure:** [`docs/REPEATER_RELEASE_PROCEDURE.md`](../../../docs/REPEATER_RELEASE_PROCEDURE.md), [`scripts/copy-repeater-release-bins.sh`](../../../scripts/copy-repeater-release-bins.sh).
