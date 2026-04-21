## r1.15.0.3 — 2026-04-21

**Repeater TCP version string (on device):** `meshcomod-r1.15.0.3-repeater-tcp-<gitsha>`.

**Highlights**

- **Xiao S3 WIO SX1262:** New repeater TCP build **`Xiao_S3_WIO_repeater_tcp`** (Wi-Fi TCP + WebSocket).
- **Repeater release tooling:** `copy-repeater-release-bins.sh` / `validate-prebuilt-release-folder.sh` now include Xiao repeater artifacts.
- **Full repeater TCP matrix:** V4 OLED, V4 TFT+touch, V3, Wireless Paper, and Xiao S3 WIO `*_repeater_tcp` app and merged images.

**Prebuilts**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_repeater_tcp-merged.bin](heltec_v4_repeater_tcp-merged.bin) | [heltec_v4_repeater_tcp.bin](heltec_v4_repeater_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_repeater_tcp-merged.bin](heltec_v4_tft_repeater_tcp-merged.bin) | [heltec_v4_tft_repeater_tcp.bin](heltec_v4_tft_repeater_tcp.bin) |
| Heltec V3 | [Heltec_v3_repeater_tcp-merged.bin](Heltec_v3_repeater_tcp-merged.bin) | [Heltec_v3_repeater_tcp.bin](Heltec_v3_repeater_tcp.bin) |
| Heltec Wireless Paper (E213) | [Heltec_Wireless_Paper_repeater_tcp-merged.bin](Heltec_Wireless_Paper_repeater_tcp-merged.bin) | [Heltec_Wireless_Paper_repeater_tcp.bin](Heltec_Wireless_Paper_repeater_tcp.bin) |
| Seeed Xiao S3 WIO SX1262 | [Xiao_S3_WIO_repeater_tcp-merged.bin](Xiao_S3_WIO_repeater_tcp-merged.bin) | [Xiao_S3_WIO_repeater_tcp.bin](Xiao_S3_WIO_repeater_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `REPEATER_FIRMWARE_VERSION=r1.15.0.3-repeater-tcp`, git **`aefa9d2e`**.

**Procedure:** [`docs/REPEATER_RELEASE_PROCEDURE.md`](../../../docs/REPEATER_RELEASE_PROCEDURE.md), [`scripts/copy-repeater-release-bins.sh`](../../../scripts/copy-repeater-release-bins.sh).
