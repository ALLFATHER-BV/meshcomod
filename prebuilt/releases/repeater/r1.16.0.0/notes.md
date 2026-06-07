## r1.16.0.0 — 2026-06-07

**Repeater TCP version string (on device):** `meshcomod-r1.16.0.0-repeater-tcp-<gitsha>`.

**Highlights**

- **Rebased on MeshCore 1.16.0.** Repeater TCP matrix rebuilt on the upstream 1.16.0
  base. Mesh-protocol/radio changes (6-byte ACK, un-scoped flood, raw packet,
  longer low-SF preamble, EU 869.495 MHz repeat freq) carry over; the meshcomod
  TCP/WS companion bridge is unchanged.
- Full repeater TCP matrix: V4 OLED, V4 TFT+touch, V3, Wireless Paper, Xiao S3 WIO
  `*_repeater_tcp` app + merged images.

**Prebuilts**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_repeater_tcp-merged.bin](heltec_v4_repeater_tcp-merged.bin) | [heltec_v4_repeater_tcp.bin](heltec_v4_repeater_tcp.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_repeater_tcp-merged.bin](heltec_v4_tft_repeater_tcp-merged.bin) | [heltec_v4_tft_repeater_tcp.bin](heltec_v4_tft_repeater_tcp.bin) |
| Heltec V3 | [Heltec_v3_repeater_tcp-merged.bin](Heltec_v3_repeater_tcp-merged.bin) | [Heltec_v3_repeater_tcp.bin](Heltec_v3_repeater_tcp.bin) |
| Heltec Wireless Paper (E213) | [Heltec_Wireless_Paper_repeater_tcp-merged.bin](Heltec_Wireless_Paper_repeater_tcp-merged.bin) | [Heltec_Wireless_Paper_repeater_tcp.bin](Heltec_Wireless_Paper_repeater_tcp.bin) |
| Seeed Xiao S3 WIO SX1262 | [Xiao_S3_WIO_repeater_tcp-merged.bin](Xiao_S3_WIO_repeater_tcp-merged.bin) | [Xiao_S3_WIO_repeater_tcp.bin](Xiao_S3_WIO_repeater_tcp.bin) |

**Build:** `REPEATER_FIRMWARE_VERSION=r1.16.0.0-repeater-tcp`, git **`f561fef3`**.
